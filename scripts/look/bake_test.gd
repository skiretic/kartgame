@tool
extends Node3D

## The M1 GI test scene: does baked light actually bounce, and does SDFGI agree?
##
## A flat plane cannot answer that. Bounce light is only visible where something
## blocks the direct light and something else colored is standing next to it, so
## this scene is a partially enclosed bay — three walls, an overhang, and a white
## column standing in the shade underneath it. The left wall is saturated red and
## takes the sun on its inner face; the back wall, the overhang soffit and the
## column are neutral white, and nothing else in the scene is colored at all. If
## the GI works, those white surfaces go pink on the side facing the red wall. If
## it does not, they stay gray. There is no way to misread that.
##
## Everything is built in code for the same reason `lookdev.gd` is: a still has
## to be reproducible from the command that produced it.
##
## **This script is `@tool`, and that is not decoration.** `LightmapGI` bakes
## whatever geometry is in the tree *in the editor*, so a scene whose geometry
## only exists once the game is running has nothing to bake — the bake reports
## "no meshes" and stops. Building the scene under `@tool` means the editor sees
## exactly the same geometry the game will, which is the only way a code-built
## scene can be lightmapped at all. That constraint carries straight through to
## the generated track in M5.
##
## Arguments (all optional, all after a bare `--`):
##
##   --gi=baked          none | sdfgi | baked — the comparison this scene exists
##                       to make. `baked` needs a bake to have happened first;
##                       see tools/bake/bake.sh.
##   --ambient=auto      auto | on | off. Sky ambient light. `auto` is on for
##                       --gi=none and off otherwise, because SDFGI and the bake
##                       both deliver the sky themselves and adding flat ambient
##                       on top double-counts it.
##   --sun_elevation=50  degrees above the horizon
##   --sun_azimuth=40    degrees. The default is chosen, not arbitrary: it puts
##                       direct sun on the *inner* face of the red wall, which is
##                       what makes the bounce exist at all.
##   --sun_lux=100000    sun illuminance; ~100,000 lx is a clear midday sun
##   --light_bake=dynamic  dynamic | static | disabled — Light3D.light_bake_mode.
##                       `dynamic` bakes indirect only and leaves direct light
##                       real-time, so the three GI modes differ *only* in their
##                       indirect term. That is what makes the comparison honest.
##   --aperture=16       f-number
##   --shutter_speed=100 denominator, so 100 means 1/100 s
##   --iso=100           sensor sensitivity
##   --texel_scale=2.0   LightmapGI texel density multiplier
##   --bake_quality=3    0 low, 1 medium, 2 high, 3 ultra
##   --bounces=3         light bounces in the bake
##   --bake_camera_attributes=false  see _configure_lightmap. Setting it true
##                       blows the lightmap out to white under physical light
##                       units, and the flag exists so that is reproducible
##                       rather than a claim.
##   --sdfgi_cell=0.2    SDFGI smallest cell size, meters
##   --ssao=false        off by default here, see _build_environment
##   --ssil=false        off by default here — it is screen-space bounce, and
##                       leaving it on would put a fake version of the exact
##                       effect under test into all three images
##   --taa=true
##   --camera_height=2.0
##   --camera_z=7.0
##   --fov=32            a long lens, see _build_camera
##   --bake=false        editor only: drive a bake and quit. tools/bake/bake.sh
##                       passes this; nothing else should.

# 1 unit = 1 meter, everywhere (ARCHITECTURE.md §5 item 1).
const GROUND_SIZE := 60.0
## Six meters, not ten. Bounce falls off with distance like everything else, and
## a bay wide enough to be roomy is a bay where the colored wall is too far from
## the white surfaces for the tint to be worth photographing.
const BAY_WIDTH := 6.0
const BAY_DEPTH := 6.0
const WALL_HEIGHT := 4.0
const WALL_THICKNESS := 0.3
## The overhang covers the back half of the bay. A roof over the whole thing
## would put the interior in uniform shade and there would be no lit red surface
## left to bounce from; no roof at all and nothing is dark enough for the bounce
## to be visible against. Half is the useful case.
const OVERHANG_DEPTH := 3.0
const OVERHANG_THICKNESS := 0.3
const COLUMN_HEIGHT := 2.4
const COLUMN_WIDTH := 0.5
## Off center, toward the red wall. Centered it reads as a symmetry test, which
## is not what it is — it is a probe, and it wants to be near the thing being
## probed.
const COLUMN_X := -1.2

## Real albedos, not screen colors. Fresh white paint measures around 0.8, dry
## asphalt around 0.1–0.2, and a saturated red pigment reflects most of the red
## channel and almost nothing else — which is precisely why it makes a strong
## colored bounce.
const ALBEDO_NEUTRAL := Color(0.80, 0.80, 0.80)
const ALBEDO_RED := Color(0.75, 0.04, 0.03)
const ALBEDO_BLUE := Color(0.05, 0.10, 0.60)
const ALBEDO_GROUND := Color(0.20, 0.20, 0.21)
## The bay floor is concrete, not white paint, and the difference is not
## decoration. A white floor is the largest sunlit surface in the scene, so it
## becomes the dominant secondary source and floods the shaded end with neutral
## light — the red bounce is still there and is diluted to almost nothing. At a
## realistic 0.32 the lit red wall is the brightest thing the shade can see, and
## the tint reads immediately. Worth remembering at M5: asphalt is dark, and a
## dark track surface is why bounce off curbs and barriers will matter there.
const ALBEDO_FLOOR := Color(0.32, 0.32, 0.32)

## Where the bake writes. The `.lmbake` holds the LightmapGIData resource; the
## lightmap itself lands beside it as a `.exr` texture array (one slice per
## atlas page) with a generated `.import` next to it.
const BAKE_PATH := "res://scenes/look/bake_test.lmbake"

var _lightmap: LightmapGI
var _baked_data: LightmapGIData
var _environment: Environment
var _camera: Camera3D
var _sun: DirectionalLight3D


## Takes the baked data off the node before the node can try to use it.
##
## A parent's `_enter_tree` runs before its children enter the tree, and a
## child's `_ready` runs before its parent's. `LightmapGI` binds its data to the
## meshes the moment it enters the tree — which, for a scene whose geometry is
## built in the parent's `_ready`, is before any of that geometry exists. It
## finds nothing, warns that it "couldn't find previously baked nodes and needs a
## rebake", and the scene stays unlit until something re-binds it.
##
## Holding the data here and putting it back in `_apply_gi_mode`, after the
## geometry is built, is the fix. It is also the reason to know the ordering:
## every procedurally built scene in this project will hit it, and the symptom is
## a warning that tells you to do the one thing that will not help.
func _enter_tree() -> void:
	# The node lives in the .tscn rather than being created here, because the
	# baked data is a property on it and a property on a node that only exists at
	# run time has nowhere to be saved.
	_lightmap = get_node_or_null("LightmapGI") as LightmapGI
	if _lightmap == null or Engine.is_editor_hint():
		return
	_baked_data = _lightmap.light_data
	_lightmap.light_data = null


func _ready() -> void:
	var args := Cmdline.parse()

	var attributes := _camera_attributes(args)

	_build_environment(args)
	_build_sun(args)
	_build_ground()
	_build_bay()
	_configure_lightmap(args, attributes)

	if Engine.is_editor_hint():
		if Cmdline.as_bool(args, "bake", false):
			_run_editor_bake()
		return

	_apply_gi_mode(args)
	_build_camera(args, attributes)
	_build_caption(args)


# --- construction ----------------------------------------------------------


func _build_environment(args: Dictionary) -> void:
	var sky_material := PhysicalSkyMaterial.new()
	sky_material.sun_disk_scale = 1.0

	var sky := Sky.new()
	sky.sky_material = sky_material

	_environment = Environment.new()
	_environment.background_mode = Environment.BG_SKY
	_environment.sky = sky
	_environment.tonemap_mode = Environment.TONE_MAPPER_AGX
	_environment.tonemap_exposure = 1.0
	# Set properly by _apply_gi_mode at run time. The editor — and therefore the
	# bake, which reads the scene's own environment — gets sky ambient off,
	# because the bake is what supplies the sky term.
	_environment.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED

	# Both default off here, unlike lookdev.gd. SSIL is screen-space indirect
	# light: it approximates exactly the effect this scene exists to measure, so
	# leaving it on would put a plausible-looking bounce into the --gi=none image
	# and the comparison would prove nothing. SSAO is off for the weaker version
	# of the same reason — it darkens creases, which is half of what the reader
	# is being asked to judge.
	if Cmdline.as_bool(args, "ssao", false):
		_environment.ssao_enabled = true
		_environment.ssao_radius = 0.5
		_environment.ssao_intensity = 1.5
	if Cmdline.as_bool(args, "ssil", false):
		_environment.ssil_enabled = true
		_environment.ssil_radius = 3.0

	var world_environment := WorldEnvironment.new()
	world_environment.name = "WorldEnvironment"
	world_environment.environment = _environment
	add_child(world_environment)


func _build_sun(args: Dictionary) -> void:
	_sun = DirectionalLight3D.new()
	_sun.name = "Sun"
	# Physical light units are on project-wide, so this is illuminance in lux.
	_sun.light_intensity_lux = Cmdline.as_float(args, "sun_lux", 100000.0)
	_sun.light_temperature = 5500.0
	_sun.light_angular_distance = 0.53
	_sun.shadow_enabled = true
	_sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	_sun.directional_shadow_max_distance = 120.0

	# What the sun contributes to the bake. `dynamic` bakes the light's indirect
	# contribution only and leaves its direct contribution to the real-time
	# renderer, so direct light and its shadows are pixel-identical in all three
	# GI modes and the only thing that changes between the stills is the bounce.
	# `static` bakes direct light too — higher quality soft shadows on static
	# geometry, but nothing dynamic is lit by it, which is wrong for a track full
	# of moving karts.
	match Cmdline.as_string(args, "light_bake", "dynamic"):
		"static":
			_sun.light_bake_mode = Light3D.BAKE_STATIC
		"disabled":
			_sun.light_bake_mode = Light3D.BAKE_DISABLED
		_:
			_sun.light_bake_mode = Light3D.BAKE_DYNAMIC

	var elevation := Cmdline.as_float(args, "sun_elevation", 50.0)
	var azimuth := Cmdline.as_float(args, "sun_azimuth", 40.0)
	_sun.rotation_degrees = Vector3(-elevation, azimuth, 0.0)
	add_child(_sun)


func _build_ground() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(GROUND_SIZE, GROUND_SIZE)
	plane.subdivide_width = 24
	plane.subdivide_depth = 24
	# Ground is context, not subject. Half density keeps 60 m of it from taking
	# more of the atlas than the eight meters that are actually being judged.
	add_child(_static_mesh("Ground", plane, ALBEDO_GROUND, Vector3.ZERO, 0.5))


## Three walls, an overhang, a floor slab, and a column in the shade.
##
## Only the red wall is colored on the side the sun reaches. The blue wall is
## opposite it and sees sky only, which is deliberate: the two walls are
## identical geometry under identical settings, so the difference between a
## strong red cast on one side of the column and a barely-there cool cast on the
## other is attributable to the direct light landing on one of them and not to
## anything about the materials.
func _build_bay() -> void:
	var half_width := BAY_WIDTH * 0.5
	var outer_width := BAY_WIDTH + 2.0 * WALL_THICKNESS
	var center_z := -BAY_DEPTH * 0.5

	# Slightly proud of the ground plane so the two do not z-fight.
	add_child(_static_box(
		"BayFloor", Vector3(BAY_WIDTH, 0.1, BAY_DEPTH), ALBEDO_FLOOR,
		Vector3(0.0, 0.05, center_z), 2.0,
	))

	add_child(_static_box(
		"WallRed", Vector3(WALL_THICKNESS, WALL_HEIGHT, BAY_DEPTH), ALBEDO_RED,
		Vector3(-(half_width + WALL_THICKNESS * 0.5), WALL_HEIGHT * 0.5, center_z), 1.0,
	))
	add_child(_static_box(
		"WallBlue", Vector3(WALL_THICKNESS, WALL_HEIGHT, BAY_DEPTH), ALBEDO_BLUE,
		Vector3(half_width + WALL_THICKNESS * 0.5, WALL_HEIGHT * 0.5, center_z), 1.0,
	))
	add_child(_static_box(
		"WallBack", Vector3(outer_width, WALL_HEIGHT, WALL_THICKNESS), ALBEDO_NEUTRAL,
		Vector3(0.0, WALL_HEIGHT * 0.5, -(BAY_DEPTH + WALL_THICKNESS * 0.5)), 1.0,
	))
	add_child(_static_box(
		"Overhang", Vector3(outer_width, OVERHANG_THICKNESS, OVERHANG_DEPTH), ALBEDO_NEUTRAL,
		Vector3(0.0, WALL_HEIGHT + OVERHANG_THICKNESS * 0.5, -(BAY_DEPTH - OVERHANG_DEPTH * 0.5)), 1.0,
	))

	# The measurement. It stands in full shade under the overhang, so every
	# photon that reaches it has bounced off something first.
	add_child(_static_box(
		"Column", Vector3(COLUMN_WIDTH, COLUMN_HEIGHT, COLUMN_WIDTH), ALBEDO_NEUTRAL,
		Vector3(COLUMN_X, COLUMN_HEIGHT * 0.5, -(BAY_DEPTH - OVERHANG_DEPTH * 0.5)), 2.0,
	))


func _static_box(
	name: String, size: Vector3, albedo: Color, origin: Vector3, texel_scale: float
) -> MeshInstance3D:
	var box := BoxMesh.new()
	box.size = size
	return _static_mesh(name, box, albedo, origin, texel_scale)


## A MeshInstance3D that `LightmapGI` will actually pick up.
##
## Two conditions, and both are silent failures when unmet — the bake simply
## reports that it found no meshes:
##
## 1. `gi_mode = GI_MODE_STATIC`. The default is `GI_MODE_DYNAMIC`, which means
##    "receive GI from probes", not "contribute to a bake".
## 2. A second UV set. Primitive meshes have none by default; `add_uv2 = true`
##    generates one, laid out so no two faces overlap, and sets the mesh's
##    `lightmap_size_hint` at a fixed five texels per meter. Imported geometry
##    gets there a different way — Blender's lightmap UVs (M2/M5) or
##    `ArrayMesh.lightmap_unwrap()`, which runs xatlas.
##
## `lightmap_texel_scale` then multiplies that size hint per instance, which is
## how the column gets four times the resolution of the ground it stands on
## without either of them needing a hand-authored lightmap size.
func _static_mesh(
	name: String, mesh: PrimitiveMesh, albedo: Color, origin: Vector3, texel_scale: float
) -> MeshInstance3D:
	mesh.add_uv2 = true

	var material := StandardMaterial3D.new()
	material.albedo_color = albedo
	material.roughness = 0.9
	material.metallic = 0.0

	var instance := MeshInstance3D.new()
	instance.name = name
	instance.mesh = mesh
	instance.material_override = material
	instance.gi_mode = GeometryInstance3D.GI_MODE_STATIC
	instance.gi_lightmap_texel_scale = texel_scale
	instance.position = origin
	return instance


func _configure_lightmap(args: Dictionary, attributes: CameraAttributesPhysical) -> void:
	if _lightmap == null:
		return

	# The inspector calls this "Quality"; the setter is set_bake_quality().
	# Ultra, and double the density the primitives hand out. Measured on this
	# scene at 512x512: low 0.7 s, medium 1.2 s, high 3.1 s, ultra 11.0 s. Ultra
	# is the first setting where the shaded end of the bay is free of the
	# low-frequency mottle the denoiser leaves behind at lower sample counts, and
	# eleven seconds is nothing here. It will not be nothing on a track, so both
	# numbers are parameters — this default is chosen for a clean test image, not
	# as a recommendation for M5.
	_lightmap.quality = Cmdline.as_int(args, "bake_quality", LightmapGI.BAKE_QUALITY_ULTRA)
	_lightmap.bounces = Cmdline.as_int(args, "bounces", 3)
	_lightmap.texel_scale = Cmdline.as_float(args, "texel_scale", 2.0)
	_lightmap.max_texture_size = Cmdline.as_int(args, "max_texture_size", 4096)
	# The bake reads the scene's own Environment for sky light, which is why the
	# WorldEnvironment above has to be built under @tool as well.
	_lightmap.environment_mode = LightmapGI.ENVIRONMENT_MODE_SCENE
	# Probes are how anything that moves — a kart — picks up the baked bounce.
	_lightmap.generate_probes_subdiv = LightmapGI.GENERATE_PROBES_SUBDIV_8
	_lightmap.interior = false

	# **Left null on purpose, and it took a blown-out bake to establish why.**
	#
	# `camera_attributes` does not tell the bake what exposure to bake at. All it
	# does is set `baked_exposure` in the .lmbake, and the renderer then scales
	# the lightmap by `camera_exposure_normalization / baked_exposure`. The
	# texture itself holds physical radiance either way.
	#
	# So handing it the same CameraAttributesPhysical the camera uses — the
	# obvious thing to do, and what "specifies exposure levels to bake at" reads
	# like it is asking for — makes that ratio exactly 1. Measured here:
	# `baked_exposure` came out as 3.2552e-05, which is precisely
	# `calculate_exposure_normalization()` for f/16, 1/100 s, ISO 100, which is
	# also what the camera applies. The two cancel, unscaled physical radiance
	# reaches the tonemapper, and every lightmapped surface renders pure white
	# while the sky and the direct term are exposed correctly beside it.
	#
	# Null leaves `baked_exposure` at 1.0, the ratio becomes the camera's own
	# normalization, and the baked term lands in the same space as the real-time
	# direct term. Set --bake_camera_attributes=true to reproduce the failure.
	_lightmap.camera_attributes = (
		attributes if Cmdline.as_bool(args, "bake_camera_attributes", false) else null
	)


# --- run time --------------------------------------------------------------


## Switches between the three GI modes the comparison needs.
##
## `_enter_tree` has already taken the baked data off the node, so `none` and
## `sdfgi` get it by simply never putting it back, and `baked` gets it by putting
## it back now that there is geometry for it to bind to.
func _apply_gi_mode(args: Dictionary) -> void:
	var mode := Cmdline.as_string(args, "gi", "baked")
	var ambient := Cmdline.as_string(args, "ambient", "auto")

	match mode:
		"none":
			pass
		"sdfgi":
			_environment.sdfgi_enabled = true
			_environment.sdfgi_cascades = 4
			# The walls here are 0.3 m thick and SDFGI voxelizes the world, so
			# the cell size is the whole story: at the 0.2 m default a wall is
			# barely more than one cell and light leaks through it.
			_environment.sdfgi_min_cell_size = Cmdline.as_float(args, "sdfgi_cell", 0.2)
			_environment.sdfgi_use_occlusion = true
			_environment.sdfgi_bounce_feedback = 0.5
			_environment.sdfgi_read_sky_light = true
			_environment.sdfgi_energy = 1.0
		"baked":
			if _baked_data == null:
				push_warning(
					"bake_test: --gi=baked but %s has no baked data. "
					% BAKE_PATH + "Run tools/bake/bake.sh first."
				)
			else:
				_lightmap.light_data = _baked_data
		_:
			push_error("bake_test: --gi must be none, sdfgi or baked, got '%s'" % mode)

	var ambient_on := ambient == "on" or (ambient == "auto" and mode == "none")
	if ambient_on:
		_environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
		_environment.ambient_light_sky_contribution = 1.0
	else:
		_environment.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED


func _build_camera(args: Dictionary, attributes: CameraAttributesPhysical) -> void:
	_camera = Camera3D.new()
	_camera.name = "BakeTestCamera"
	# A long lens rather than a wide one. The bay is 6.6 m across and the subject
	# is a color cast on part of it; at the 70 degrees lookdev.gd uses for a road
	# it would be a tenth of the frame.
	_camera.fov = Cmdline.as_float(args, "fov", 32.0)
	_camera.near = 0.05
	_camera.far = 500.0
	_camera.attributes = attributes
	_camera.position = Vector3(
		Cmdline.as_float(args, "camera_x", 0.0),
		Cmdline.as_float(args, "camera_height", 2.0),
		Cmdline.as_float(args, "camera_z", 7.0),
	)
	add_child(_camera)
	# Aimed at the shaded end of the bay, which is where the bounce lives. The
	# lit half is in frame but is not what the picture is about.
	_camera.look_at(Vector3(0.0, 1.3, -BAY_DEPTH), Vector3.UP)

	var viewport := get_viewport()
	viewport.use_taa = Cmdline.as_bool(args, "taa", true)
	viewport.msaa_3d = Viewport.MSAA_DISABLED


## Exposure as a real camera states it — the sunny 16 rule, same as lookdev.gd.
## Shared with the bake, see _configure_lightmap.
func _camera_attributes(args: Dictionary) -> CameraAttributesPhysical:
	var attributes := CameraAttributesPhysical.new()
	attributes.exposure_aperture = Cmdline.as_float(args, "aperture", 16.0)
	attributes.exposure_shutter_speed = Cmdline.as_float(args, "shutter_speed", 100.0)
	attributes.exposure_sensitivity = Cmdline.as_float(args, "iso", 100.0)
	attributes.auto_exposure_enabled = false
	return attributes


## Every still says which of the three it is, because three images of the same
## geometry that differ only in a subtle color cast are exactly the case where a
## mislabeled file wastes an afternoon.
func _build_caption(args: Dictionary) -> void:
	var layer := CanvasLayer.new()
	add_child(layer)

	var mode := Cmdline.as_string(args, "gi", "baked")
	var baked_note := "no data"
	if _lightmap != null and _lightmap.light_data != null:
		baked_note = "%d users" % _lightmap.light_data.get_user_count()

	var caption := Label.new()
	caption.add_theme_font_size_override("font_size", 20)
	caption.add_theme_color_override("font_color", Color.WHITE)
	caption.add_theme_color_override("font_outline_color", Color.BLACK)
	caption.add_theme_constant_override("outline_size", 6)
	caption.position = Vector2(24, 24)
	caption.text = "\n".join([
		"GI %s   lightmap %s   ambient %s" % [
			mode,
			baked_note,
			"on" if _environment.ambient_light_source == Environment.AMBIENT_SOURCE_SKY else "off",
		],
		"sun %.0f lx at %.0f deg elev / %.0f deg az   bake mode %s" % [
			Cmdline.as_float(args, "sun_lux", 100000.0),
			Cmdline.as_float(args, "sun_elevation", 50.0),
			Cmdline.as_float(args, "sun_azimuth", 40.0),
			Cmdline.as_string(args, "light_bake", "dynamic"),
		],
		"f/%.0f  1/%.0f s  ISO %.0f   AgX   taa %s" % [
			Cmdline.as_float(args, "aperture", 16.0),
			Cmdline.as_float(args, "shutter_speed", 100.0),
			Cmdline.as_float(args, "iso", 100.0),
			"on" if Cmdline.as_bool(args, "taa", true) else "off",
		],
	])
	layer.add_child(caption)


# --- editor ----------------------------------------------------------------


## Hands off to the bake driver, which is loaded rather than preloaded because
## it talks to `EditorInterface` and that class does not exist outside an editor
## build — a preload would make this script fail to parse in an exported game.
func _run_editor_bake() -> void:
	var driver: GDScript = load("res://tools/bake/editor_bake.gd")
	driver.bake_and_quit(self, _lightmap, BAKE_PATH)
