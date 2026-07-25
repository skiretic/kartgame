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

## ARCHITECTURE.md §5 item 2 fixes the track surface at 512 px/m. The photoscans
## are 2048 px square, so one tile has to cover 2048 / 512 = 4.00 m exactly.
## Holding this everywhere is the point — mismatched texel density is named there
## as the most common tell in amateur work, and it is only avoidable if the
## number is derived rather than dialed in.
const TRACK_TEXEL_DENSITY := 512.0
const PHOTOSCAN_RESOLUTION := 2048.0
const ASPHALT_DIRECTORY := "res://assets/materials/asphalt_track/"
const ASPHALT_PREFIX := "Asphalt020L_2K-JPG_"

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

	# Each of the following is switchable, because the point of this scene is to
	# be able to attribute what a still shows to one change at a time.

	if Cmdline.as_bool(args, "ssao", true):
		environment.ssao_enabled = true
		# Half a meter. Screen-space AO is a contact term — it is what makes a
		# post sit on the ground instead of hovering over it — and a radius much
		# larger than the contact it is describing turns into a dirty smudge
		# around every object.
		environment.ssao_radius = 0.5
		environment.ssao_intensity = 1.5
		environment.ssao_power = 1.5
		environment.ssao_detail = 0.5
		# Baked GI already carries the large-scale occlusion (issue #6), so AO is
		# held to the close range where a lightmap has no texels to spare.
		environment.ssao_light_affect = 0.2
		environment.ssao_ao_channel_affect = 0.0

	if Cmdline.as_bool(args, "ssil", true):
		environment.ssil_enabled = true
		# Bounce reaches further than contact occlusion does, so this radius is
		# deliberately several times the SSAO one.
		environment.ssil_radius = 3.0
		environment.ssil_intensity = 1.0
		environment.ssil_normal_rejection = 1.0

	if Cmdline.as_bool(args, "fog", true):
		# Depth fog, not volumetric. See the note in _build_environment's caller
		# and ADR-0021: these are different features solving different problems,
		# and aerial perspective over a kilometer is this one.
		environment.fog_enabled = true
		environment.fog_mode = Environment.FOG_MODE_DEPTH
		environment.fog_depth_begin = 30.0
		environment.fog_depth_end = 2000.0
		environment.fog_depth_curve = 1.0
		environment.fog_density = Cmdline.as_float(args, "fog_density", 0.4)
		# Taking the fog color from the sky is what makes distance read as
		# distance rather than as a gray wash laid over the picture.
		environment.fog_sky_affect = 0.0
		environment.fog_light_color = Color(0.62, 0.70, 0.80)
		environment.fog_sun_scatter = 0.2
		environment.fog_aerial_perspective = 1.0

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
	# Physical light units are on project-wide, so this is illuminance in lux and
	# not an arbitrary energy. A clear midday sun is around 100,000 lx; heavy
	# overcast is nearer 10,000. Those are the numbers to reach for when time of
	# day and weather arrive in M10, rather than a multiplier someone tuned.
	sun.light_intensity_lux = Cmdline.as_float(args, "sun_lux", 100000.0)
	# ~5,500 K is direct midday sunlight. The sky supplies the cool fill.
	sun.light_temperature = 5500.0
	# Real sun subtends about half a degree, which is what makes a shadow edge
	# soften with distance from its caster instead of staying razor sharp.
	sun.light_angular_distance = 0.53
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
		ground.material_override = _checker_material()
	else:
		ground.material_override = _asphalt_material()
	ground.name = "Ground"
	add_child(ground)


## The CC0 asphalt photoscan, tiled at exactly the §5 texel density.
##
## Every map is loaded from the same prefix so a swap to a different scan is one
## constant, and the tiling rate is computed from the density standard rather
## than typed in — the whole value of a density standard is that it is not a
## number someone chose per surface.
func _asphalt_material() -> StandardMaterial3D:
	var tile_meters := PHOTOSCAN_RESOLUTION / TRACK_TEXEL_DENSITY  # 4.00 m
	var material := StandardMaterial3D.new()

	material.albedo_texture = load(ASPHALT_DIRECTORY + ASPHALT_PREFIX + "Color.jpg")

	material.normal_enabled = true
	# ambientCG ships NormalGL and NormalDX; only the GL files were extracted,
	# because Godot expects +Y up and a DX twin sitting alongside is a mistake
	# waiting to be made. See ATTRIBUTION.md.
	material.normal_texture = load(ASPHALT_DIRECTORY + ASPHALT_PREFIX + "NormalGL.jpg")

	material.roughness_texture = load(ASPHALT_DIRECTORY + ASPHALT_PREFIX + "Roughness.jpg")
	material.roughness_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_RED
	material.roughness = 1.0

	material.ao_enabled = true
	material.ao_texture = load(ASPHALT_DIRECTORY + ASPHALT_PREFIX + "AmbientOcclusion.jpg")
	material.ao_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_RED
	# Baked GI supplies real occlusion at M1 issue #6. The map is a close-range
	# supplement, so it is dialed back rather than doubling up with the bake.
	material.ao_light_affect = 0.4

	material.metallic = 0.0
	material.uv1_scale = Vector3(GROUND_SIZE / tile_meters, GROUND_SIZE / tile_meters, 1.0)
	return material


## A 1 m checkerboard with fine grain on top.
##
## Kept after the photoscans landed, because the two answer different questions.
## The scan answers "does this read as asphalt"; the checker answers "is this the
## right size", and it answers it by being countable — one square is exactly one
## meter, so the reference geometry can be measured against the floor instead of
## judged against it.
func _checker_material() -> StandardMaterial3D:
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


## Local specular, which is the half of reflection Godot does well.
##
## ARCHITECTURE.md §4 gives probes the real reflection work and leaves SSR as a
## supplement, because Godot's SSR can only reflect what is already on screen —
## and a camera 0.6 m off the ground sees almost nothing of the world it would
## need to reflect. A probe has no such limit.
##
## UPDATE_ONCE is the right mode for a fixed sun over static geometry: it is
## captured on the first frame and then costs nothing. UPDATE_ALWAYS exists for
## moving content and is not what a racetrack is.
func _build_reflection_probe(args: Dictionary) -> void:
	if not Cmdline.as_bool(args, "probe", true):
		return

	var probe := ReflectionProbe.new()
	probe.update_mode = ReflectionProbe.UPDATE_ONCE
	probe.size = Vector3(80.0, 20.0, 120.0)
	# Origin at ground level would put half the box underground and waste the
	# capture on the inside of the plane.
	probe.origin_offset = Vector3(0.0, 0.0, 0.0)
	probe.position = Vector3(0.0, 8.0, 0.0)
	probe.max_distance = 400.0
	# Let the probe blend with the sky-derived ambient rather than replacing it;
	# a probe that fully overrides ambient makes everything inside its box read
	# as a different scene from everything outside.
	probe.interior = false
	probe.intensity = 1.0
	probe.name = "ReflectionProbe"
	add_child(probe)


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


## Exposure as a real camera states it: f-stop, shutter speed, ISO.
##
## The defaults are the sunny 16 rule — f/16 at 1/ISO seconds gives a correct
## exposure in direct sun — which is the whole reason to work in physical units.
## The scene is lit by describing the conditions, and the picture comes out at
## the right brightness because the arithmetic is real, not because anyone
## adjusted it.
##
## Auto-exposure is deliberately off. A still that adapts to its own content
## cannot be compared with the still taken before it.
func _camera_attributes(args: Dictionary) -> CameraAttributesPhysical:
	var attributes := CameraAttributesPhysical.new()
	attributes.exposure_aperture = Cmdline.as_float(args, "aperture", 16.0)
	attributes.exposure_shutter_speed = Cmdline.as_float(args, "shutter_speed", 100.0)
	attributes.exposure_sensitivity = Cmdline.as_float(args, "iso", 100.0)
	attributes.auto_exposure_enabled = false
	return attributes


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
		"sun %.0f lx at %.0f deg   f/%.0f  1/%.0f s  ISO %.0f" % [
			Cmdline.as_float(args, "sun_lux", 100000.0),
			Cmdline.as_float(args, "sun_elevation", 50.0),
			Cmdline.as_float(args, "aperture", 16.0),
			Cmdline.as_float(args, "shutter_speed", 100.0),
			Cmdline.as_float(args, "iso", 100.0),
		],
	])
	layer.add_child(_caption)


func _flat_material(albedo: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = albedo
	material.roughness = 0.6
	return material
