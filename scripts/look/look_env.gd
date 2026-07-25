class_name LookEnv
extends RefCounted

## The M1 lighting rig, in one place, because ambient light double-counts.
##
## Three separate ambient double-counting bugs turned up during M1, and all three
## were the same mistake wearing a different hat — one quantity of light arriving
## twice because two features each believed they owned it:
##
## 1. A `ReflectionProbe` at its default `AMBIENT_ENVIRONMENT` added its own
##    ambient on top of the environment's. Measured: sunlit asphalt went from
##    0.32 to 0.60 sRGB, a stop and a half of light from nowhere (ADR-0021).
## 2. An HDRI plate mixed with a physical `DirectionalLight3D`. The sun disc
##    carries 71-76% of a plate's total illuminance, so the key light was very
##    nearly doubled. Settled in ROADMAP M1: `PhysicalSkyMaterial` leads and the
##    plates became reference and reflection content.
## 3. A lightmap present while `Environment` sky ambient was still on, so the sky
##    was counted once in the bake and once live (ADR-0022).
##
## The pattern is clear enough to predict a fourth: **any new scene that builds
## its own lighting is a fresh chance to count something twice.** So scenes do not
## build their own lighting. They call this, and the reasoning behind every value
## lives here once rather than being re-derived per scene by whoever needed a new
## camera angle.
##
## Physical light units are on project-wide (`project.godot`), so every quantity
## below is a real one: the sun is in lux, the camera in f-stop, shutter and ISO.
## That is what makes a doubled light *visible* as a wrong number rather than
## merely as a picture someone has to judge.

## AgX and physical exposure are not optional per scene. ARCHITECTURE.md §4 calls
## AgX the single biggest one-line realism win — it rolls highlights off instead
## of clipping them — and a scene that quietly picks a different tonemap cannot be
## compared with any still taken before it.
static func environment(args: Dictionary) -> Environment:
	var sky_material := PhysicalSkyMaterial.new()
	sky_material.sun_disk_scale = 1.0

	var sky := Sky.new()
	sky.sky_material = sky_material

	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.tonemap_mode = Environment.TONE_MAPPER_AGX
	environment.tonemap_exposure = 1.0
	# The sky is the only ambient source. Anything else that wants to contribute
	# ambient has to be given room by turning this down, not by adding to it.
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	environment.ambient_light_sky_contribution = 1.0

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
		# Baked GI already carries the large-scale occlusion, so AO is held to the
		# close range where a lightmap has no texels to spare. This is the same
		# double-counting argument as everything else in this file.
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
		# Depth fog, not volumetric. ADR-0021: these are different features
		# solving different problems, and aerial perspective over a kilometer is
		# this one. Volumetric fog is a 64 m froxel volume for light shafts.
		environment.fog_enabled = true
		environment.fog_mode = Environment.FOG_MODE_DEPTH
		environment.fog_depth_begin = Cmdline.as_float(args, "fog_begin", 20.0)
		environment.fog_depth_end = Cmdline.as_float(args, "fog_end", 800.0)
		environment.fog_depth_curve = Cmdline.as_float(args, "fog_curve", 1.0)
		environment.fog_density = Cmdline.as_float(args, "fog_density", 1.0)
		environment.fog_light_color = Color(
			Cmdline.as_float(args, "fog_r", 0.68),
			Cmdline.as_float(args, "fog_g", 0.76),
			Cmdline.as_float(args, "fog_b", 0.88))
		# Physical light units mean fog luminance has to be on the same scale as
		# everything else. A default energy of 1.0 against a sunlit surface at a
		# few thousand cd/m2 is effectively black, which is why fog tuned by eye
		# in arbitrary units darkens the distance instead of lightening it.
		# Inert while fog_aerial_perspective is 1.0: in that mode the fog takes its
		# color from the sky per view direction and ignores fog_light_color and
		# fog_light_energy entirely. Verified by sweeping energy across 1, 300 and
		# 900 and getting byte-identical renders. Kept switchable because dropping
		# aerial perspective to 0 hands control back to these, and that is the
		# lever for weather that is not just haze.
		environment.fog_light_energy = Cmdline.as_float(args, "fog_energy", 1.0)
		environment.fog_sun_scatter = 0.2
		environment.fog_aerial_perspective = Cmdline.as_float(args, "fog_aerial", 1.0)
		environment.fog_sky_affect = Cmdline.as_float(args, "fog_sky", 0.0)

	return environment


## The sun, in lux, at a real angle.
##
## ARCHITECTURE.md §4 wants time of day to be a lever rather than a decoration,
## which means elevation and azimuth are arguments and the intensity is a
## measured quantity: ~100,000 lx is a clear midday sun, ~10,000 is heavy
## overcast. Those are the numbers M10's weather work reaches for, rather than a
## multiplier someone tuned until a screenshot looked right.
static func sun(args: Dictionary) -> DirectionalLight3D:
	var light := DirectionalLight3D.new()
	light.light_intensity_lux = Cmdline.as_float(args, "sun_lux", 100000.0)
	# ~5,500 K is direct midday sunlight. The sky supplies the cool fill.
	light.light_temperature = 5500.0
	# Real sun subtends about half a degree, which is what makes a shadow edge
	# soften with distance from its caster instead of staying razor sharp.
	light.light_angular_distance = 0.53
	light.shadow_enabled = true
	# ARCHITECTURE.md §4 asks for four splits with split 0 tight, because the
	# cockpit camera sits ~0.6 m off the ground and self-shadowing there is what
	# sets the tightness. The cockpit does not exist until M4; the intent is
	# recorded here so the value is a decision rather than a default.
	light.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	light.directional_shadow_split_1 = 0.03
	light.directional_shadow_split_2 = 0.12
	light.directional_shadow_split_3 = 0.4
	light.directional_shadow_max_distance = Cmdline.as_float(args, "shadow_distance", 200.0)

	var elevation := Cmdline.as_float(args, "sun_elevation", 50.0)
	var azimuth := Cmdline.as_float(args, "sun_azimuth", -35.0)
	light.rotation_degrees = Vector3(-elevation, azimuth, 0.0)
	light.name = "Sun"
	return light


## Local specular, which is the half of reflection Godot does well.
##
## ARCHITECTURE.md §4 gives probes the real reflection work and leaves SSR as a
## supplement, because Godot's SSR can only reflect what is already on screen —
## and a camera 0.6 m off the ground sees almost nothing of the world it would
## need to reflect. A probe has no such limit.
##
## `UPDATE_ONCE` is the right mode for a fixed sun over static geometry: captured
## on the first frame, then free. `UPDATE_ALWAYS` exists for moving content and a
## racetrack is not that.
##
## **`ambient_mode` is `AMBIENT_DISABLED` and that is load-bearing.** The default,
## `AMBIENT_ENVIRONMENT`, adds the probe's own ambient term on top of the
## environment's — the first of the three M1 double-counts, and the reason this
## helper exists at all. A probe supplies specular here. Nothing else.
static func reflection_probe(
	size: Vector3 = Vector3(80.0, 20.0, 120.0),
	position: Vector3 = Vector3(0.0, 8.0, 0.0),
	max_distance: float = 400.0
) -> ReflectionProbe:
	var probe := ReflectionProbe.new()
	probe.update_mode = ReflectionProbe.UPDATE_ONCE
	probe.size = size
	# Origin at ground level would put half the box underground and waste the
	# capture on the inside of the plane.
	probe.origin_offset = Vector3(0.0, 0.0, 0.0)
	probe.position = position
	probe.max_distance = max_distance
	# Let the probe blend with the sky-derived ambient rather than replacing it;
	# a probe that fully overrides ambient makes everything inside its box read
	# as a different scene from everything outside.
	probe.interior = false
	probe.intensity = 1.0
	probe.ambient_mode = ReflectionProbe.AMBIENT_DISABLED
	probe.name = "ReflectionProbe"
	return probe


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
static func camera_attributes(args: Dictionary) -> CameraAttributesPhysical:
	var attributes := CameraAttributesPhysical.new()
	attributes.exposure_aperture = Cmdline.as_float(args, "aperture", 16.0)
	attributes.exposure_shutter_speed = Cmdline.as_float(args, "shutter_speed", 100.0)
	attributes.exposure_sensitivity = Cmdline.as_float(args, "iso", 100.0)
	attributes.auto_exposure_enabled = false
	return attributes


## ARCHITECTURE.md §5 item 2 fixes the track surface at 512 px/m.
##
## The photoscans are 2048 px square, so one tile has to cover 2048 / 512 = 4.00 m
## exactly. Holding this everywhere is the point — mismatched texel density is
## named in §5 as the most common tell in amateur work, and it is only avoidable
## if the number is derived rather than dialed in per surface.
const TRACK_TEXEL_DENSITY := 512.0
const PHOTOSCAN_RESOLUTION := 2048.0
const ASPHALT_DIRECTORY := "res://assets/materials/asphalt_track/"
const ASPHALT_PREFIX := "Asphalt020L_2K-JPG_"


## The CC0 asphalt photoscan, tiled at exactly the §5 texel density.
##
## Every map is loaded from the same prefix so a swap to a different scan is one
## constant, and the tiling rate is computed from the density standard rather
## than typed in — the whole value of a density standard is that it is not a
## number someone chose per surface.
static func asphalt_material(extent: float) -> StandardMaterial3D:
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
	# Baked GI supplies real occlusion. The map is a close-range supplement, so it
	# is dialed back rather than doubling up with the bake.
	material.ao_light_affect = 0.4

	material.metallic = 0.0
	material.uv1_scale = Vector3(extent / tile_meters, extent / tile_meters, 1.0)
	return material


## A 1 m checkerboard with fine grain on top.
##
## Kept alongside the photoscans because the two answer different questions. The
## scan answers "does this read as asphalt"; the checker answers "is this the
## right size", and it answers it by being countable — one square is exactly one
## meter, so geometry can be measured against the floor instead of judged against
## it. That is what makes it the right ground for the M2 kart turntable, where
## scale is the acceptance criterion.
static func checker_material(extent: float) -> StandardMaterial3D:
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
	material.uv1_scale = Vector3(extent / CELLS, extent / CELLS, 1.0)
	return material


## The exposure line every caption carries, so a still states its own conditions.
static func exposure_caption(args: Dictionary) -> String:
	return "sun %.0f lx at %.0f deg   f/%.0f  1/%.0f s  ISO %.0f" % [
		Cmdline.as_float(args, "sun_lux", 100000.0),
		Cmdline.as_float(args, "sun_elevation", 50.0),
		Cmdline.as_float(args, "aperture", 16.0),
		Cmdline.as_float(args, "shutter_speed", 100.0),
		Cmdline.as_float(args, "iso", 100.0),
	]
