@tool
extends Node3D

## The circuit's static half, in a scene `LightmapGI` can actually see. Issue #182.
##
##     tools/bake/bake.sh --scene=res://scenes/game/valdirone_bake.tscn
##     tools/bake/bake.sh --scene=res://scenes/game/valdirone_bake.tscn \
##                        --preflight-only
##
## ## Why this is a second scene and not `valdirone.tscn`
##
## `LightmapGI` bakes what is in the tree **in the editor**, so a scene built at
## run time has nothing to bake — `bake_test.gd`'s header says the same thing and
## ends with "that constraint carries straight through to the generated track in
## M5". This is M5. The obvious move is to make `circuit.gd` a `@tool` script, and
## it is the wrong one: `circuit.gd` builds a `KartBody`, a `PlayerDriver`, a
## `SessionRunner`, three cameras, a tuning registry and an audio rig, and every
## one of those would then be constructed inside the editor on every open. A bake
## needs the geometry and the sun. It needs none of the rest of it.
##
## So the split is by what the bake reads: this scene is the environment, the sun,
## the generated mesh and the ground, built by the same `TrackTerrain` the
## driveable scene uses, and nothing else. Both read the same `track.json`.
##
## ## What is deliberately not here
##
## **The scatter.** `LightmapGI::_find_meshes_and_lights` walks `MeshInstance3D`
## and nothing else, so a `MultiMeshInstance3D` is invisible to it — it can be
## neither lit nor an occluder. Five thousand props as individual `MeshInstance3D`
## nodes would be bakeable and would also be five thousand draw calls in the
## scene that has to run at 120 Hz. The trade is taken deliberately and the
## consequence is stated: scatter takes indirect light from the lightmap's
## probes, which is what `generate_probes_subdiv` exists for, and casts no baked
## shadow. `docs/adr_pending_182.md`.
##
## ## Nothing here sets `owner`, and that is load-bearing
##
## `LightmapGI` walks past any child whose `owner` is null, so the obvious move is
## to adopt the generated geometry in `_ready`. Doing it here cost a **2.4 MB**
## `.tscn`: an owned node is a *serialized* node, so `EditorInterface.save_scene()`
## at the end of the bake wrote all 46,000 triangles of ground and track into the
## scene file, and the next open would have built a second copy of the circuit on
## top of the one the script builds. `editor_bake.gd` already solves this — it
## adopts every unowned node for the duration of the bake and releases them before
## the save — and its `_release_generated_nodes` only releases what *it* adopted,
## so anything owned here stays owned. Leave them alone.
##
## **Sky ambient.** `AMBIENT_SOURCE_DISABLED`, in the editor and at run time both.
## A lightmap present while `Environment` sky ambient is still on counts the sky
## once in the bake and once live — that is ADR-0022, the third of the four
## ambient double-counts, and it is the one that arrives with a lightmap.
##
## Arguments, on top of everything `LookEnv` takes:
##
##   --track=res://data/tracks/valdirone_nuova.track.json
##   --layout=forward
##   --terrain-cell=5.0
##   --texel_scale=1.0    LightmapGI density multiplier over the size hints
##   --bake_quality=2     0 low, 1 medium, 2 high, 3 ultra
##   --bounces=2
##   --max_texture_size=4096
##   --bake=false         editor only; tools/bake/bake.sh passes this

const DEFAULT_TRACK := "res://data/tracks/valdirone_nuova.track.json"
const TRACK_MESH_PATH := "res://assets/generated/valdirone_nuova.glb"
const BAKE_PATH := "res://scenes/game/valdirone.lmbake"

## Lightmap texels per meter along the **coarsest** axis of a surface's UV2.
##
## Not a quality target — a budget ceiling forced by the mesh. `gentrack.py`'s
## UV2 is 120.9 : 1 anisotropic (see `_uv2_meters_per_unit`, which measures it),
## so a square atlas sized to give a sane density along the lap gives 121x that
## across the road and spends the whole budget doing it. At 0.5 a texel is 2 m
## along the lap and 1.7 cm across it, and one 704x704 slice per surface.
##
## Raising this is not the fix and the fix is not in this file. Reported.
const TRACK_TEXELS_PER_METER := 0.5

var _lightmap: LightmapGI
var _baked_data: LightmapGIData
var _environment: Environment
var _track: KartTrack
var _terrain: TrackTerrain
var _args := {}
var _sizes := {}


## `bake_test.gd`'s ordering trap, and it applies here for the same reason.
##
## `LightmapGI` binds its data to meshes the moment it enters the tree, which for
## a scene whose geometry is built in the parent's `_ready` is before any of that
## geometry exists. It finds nothing and warns that it needs a rebake. Holding the
## data here and putting it back afterwards is the fix.
func _enter_tree() -> void:
	_lightmap = get_node_or_null("LightmapGI") as LightmapGI
	if _lightmap == null or Engine.is_editor_hint():
		return
	_baked_data = _lightmap.light_data
	_lightmap.light_data = null


func _ready() -> void:
	_args = Cmdline.parse()

	_track = KartTrack.new()
	var path := Cmdline.as_string(_args, "track", DEFAULT_TRACK)
	if _track.load(path) != OK:
		push_error("%s refused:\n  %s" % [path, "\n  ".join(_track.problems())])
		return
	_track.select_layout(Cmdline.as_string(_args, "layout", "forward"))

	_build_environment()
	_build_ground()
	_build_track_mesh()
	_configure_lightmap()

	if Engine.is_editor_hint():
		if Cmdline.as_bool(_args, "bake", false):
			_run_editor_bake()
		return

	# `--gi=none` leaves the baked data off the node, which is how the bake gets
	# measured rather than judged: two stills from the same camera, one each way,
	# through `tools/shots/compare.gd`. Never `cmp` — ADR-0023, one Godot render in
	# six differs from itself by a mean of 2/255.
	if _baked_data != null and Cmdline.as_string(_args, "gi", "baked") == "baked":
		_lightmap.light_data = _baked_data
	# Without the lightmap the sky has to come from somewhere, or the comparison is
	# between a lit scene and an unlit one rather than between two GI methods.
	if _lightmap.light_data == null:
		_environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
		_environment.ambient_light_sky_contribution = 1.0
	_build_camera()
	_report()


# --- construction ----------------------------------------------------------


func _build_environment() -> void:
	_environment = LookEnv.environment(_args)
	# The one change from `LookEnv`'s defaults, and it is the ADR-0022 rule rather
	# than a preference: with a lightmap in the scene the sky is already in the
	# bake, so leaving sky ambient on counts it twice. `LookEnv` is not modified —
	# every other scene wants it on, and this is the only one with a lightmap.
	_environment.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED

	var world_environment := WorldEnvironment.new()
	world_environment.name = "WorldEnvironment"
	world_environment.environment = _environment
	add_child(world_environment)

	var sun := LookEnv.sun(_args)
	# `BAKE_DYNAMIC` bakes the sun's *indirect* contribution and leaves its direct
	# light and shadows real-time, so a kart moving through the scene is lit by the
	# same sun as the asphalt it is standing on. `BAKE_STATIC` would give the
	# static geometry better shadows and light nothing that moves, which on a
	# racetrack is the wrong half.
	sun.light_bake_mode = Light3D.BAKE_DYNAMIC
	add_child(sun)


func _build_ground() -> void:
	var box := _bounds()
	var extent := maxf(box.size.x, box.size.z) + 240.0
	var corridor := TrackCorridor.new()
	corridor.measure(_track)
	_terrain = TrackTerrain.new()
	_terrain.build(_track, corridor, extent, box.get_center(), box.position.y - 0.5,
			Cmdline.as_float(_args, "terrain-cell", 5.0))

	var visual := _terrain.visual()
	_prepare_for_bake(visual)
	add_child(visual)


## The generated `.glb`, with every surface set up to bake.
##
## The mesh already carries the UV2 channel — `gentrack.py` snakes the lap into
## ten rows, which is what keeps a 98:1 ribbon from becoming 98:1 texels. What it
## cannot carry is a `lightmap_size_hint`: glTF has no such concept, and Godot only
## writes one when its importer runs an unwrap, which would *overwrite* the
## authored UV2. So the hint is computed here, from each surface's own area.
func _build_track_mesh() -> void:
	if not ResourceLoader.exists(TRACK_MESH_PATH):
		push_error("no track mesh at %s — run tools/blender/gentrack.sh" % TRACK_MESH_PATH)
		return
	var mesh := (load(TRACK_MESH_PATH) as PackedScene).instantiate() as Node3D
	mesh.name = "TrackMesh"
	add_child(mesh)
	for node in mesh.find_children("*", "MeshInstance3D", true, false):
		_prepare_for_bake(node as MeshInstance3D)


## `gi_mode`, and a size hint that is not zero.
##
## Both are silent failures when unmet. `GI_MODE_DYNAMIC` is the default and means
## "receive GI from probes", not "contribute to a bake", so a mesh left on it is
## simply skipped — and `LightmapGI` reports the same `BAKE_ERROR_NO_MESHES` for
## that as it does for no UV2, no normals, or a hidden node. A zero size hint is
## worse than an error: it bakes, at 64x64, whatever the mesh's real size is.
func _prepare_for_bake(instance: MeshInstance3D) -> void:
	instance.gi_mode = GeometryInstance3D.GI_MODE_STATIC
	var mesh := instance.mesh as ArrayMesh
	if mesh == null:
		return
	if mesh.lightmap_size_hint == Vector2i.ZERO:
		mesh.lightmap_size_hint = _hint_for(mesh)
	var meters := _uv2_meters_per_unit(mesh)
	_sizes[instance.name] = "%s  %.2f m/texel along the coarse axis, %.1f:1 UV2 aspect" % [
		mesh.lightmap_size_hint,
		maxf(meters.x, meters.y) / float(maxi(mesh.lightmap_size_hint.x, 1)),
		maxf(meters.x, meters.y) / maxf(minf(meters.x, meters.y), 1e-6),
	]


## A square size hint sized from the mesh's own UV2 parameterization.
##
## Not from its surface area, which was the first thing tried and is wrong here.
## Every surface `gentrack.py` emits shares **one** UV2 layout — `snake_uv2` puts
## the whole lap into the unit square in ten rows — so what decides how many
## texels a surface needs is not how much surface it has but how many meters one
## UV2 unit covers. Sizing by `sqrt(area)` gave the edge lines 64x64 for 1,375 m
## of line, which is one texel per 21 m against the road's 2 m beside it: a hard
## step in the shadow exactly along the white line.
##
## The hint is set from the **coarsest** of the two axes, so nothing is under-
## resolved and the finer axis is simply wasted. On a 120.9 : 1 parameterization
## almost all of it is wasted, which is why `_prepare_for_bake` prints the number.
func _hint_for(mesh: ArrayMesh) -> Vector2i:
	var meters := _uv2_meters_per_unit(mesh)
	var coarsest := maxf(meters.x, meters.y)
	var wanted := maxi(64, int(ceil(coarsest * TRACK_TEXELS_PER_METER)))
	# A multiple of 64 rather than a power of two: the atlas packer has no such
	# requirement, and rounding 1,040 up to 2,048 doubled the ground's slice for
	# nothing.
	var side := mini(int(ceil(float(wanted) / 64.0)) * 64, 4096)
	return Vector2i(side, side)


## How many meters of surface one unit of UV2 spans, along u and along v.
##
## The standard tangent-space solve, per triangle, averaged. It is the only way to
## know what a lightmap texel will actually cover: the UV2 channel arrives inside
## a `.glb` with no metadata about what it means, and `snake_uv2`'s own docstring
## and its code disagree about which axis runs along the lap.
func _uv2_meters_per_unit(mesh: ArrayMesh) -> Vector2:
	var along_u := 0.0
	var along_v := 0.0
	var counted := 0
	for surface in mesh.get_surface_count():
		var arrays := mesh.surface_get_arrays(surface)
		var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var uv2s: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV2]
		if uv2s.is_empty():
			continue
		var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		var count := indices.size() if not indices.is_empty() else vertices.size()
		for triangle in range(0, count - 2, 3):
			var i0 := indices[triangle] if not indices.is_empty() else triangle
			var i1 := indices[triangle + 1] if not indices.is_empty() else triangle + 1
			var i2 := indices[triangle + 2] if not indices.is_empty() else triangle + 2
			var edge_1 := vertices[i1] - vertices[i0]
			var edge_2 := vertices[i2] - vertices[i0]
			var d1 := uv2s[i1] - uv2s[i0]
			var d2 := uv2s[i2] - uv2s[i0]
			var determinant := d1.x * d2.y - d2.x * d1.y
			if absf(determinant) < 1e-14:
				continue
			var inverse := 1.0 / determinant
			along_u += ((edge_1 * d2.y - edge_2 * d1.y) * inverse).length()
			along_v += ((edge_2 * d1.x - edge_1 * d2.x) * inverse).length()
			counted += 1
	if counted == 0:
		return Vector2.ONE
	return Vector2(along_u / float(counted), along_v / float(counted))


func _configure_lightmap() -> void:
	if _lightmap == null:
		return
	_lightmap.quality = Cmdline.as_int(_args, "bake_quality", LightmapGI.BAKE_QUALITY_HIGH)
	_lightmap.bounces = Cmdline.as_int(_args, "bounces", 2)
	_lightmap.texel_scale = Cmdline.as_float(_args, "texel_scale", 1.0)
	_lightmap.max_texture_size = Cmdline.as_int(_args, "max_texture_size", 4096)
	_lightmap.environment_mode = LightmapGI.ENVIRONMENT_MODE_SCENE
	# Probes are the only way anything that moves picks up the baked bounce, and on
	# this scene that is the kart *and* every one of the five thousand scatter
	# instances. Subdiv 8 over a 640 m world is a probe roughly every 2.5 m.
	_lightmap.generate_probes_subdiv = LightmapGI.GENERATE_PROBES_SUBDIV_8
	_lightmap.interior = false
	# **Null, and `bake_test.gd`'s comment is the reason.** `camera_attributes` does
	# not set the exposure the bake works at; it sets `baked_exposure`, and the
	# renderer then scales by `camera_exposure_normalization / baked_exposure`.
	# Handing it the camera's own attributes makes that ratio exactly 1, unscaled
	# physical radiance reaches the tonemapper, and every lightmapped surface
	# renders pure white.
	#
	# `--bake_camera_attributes=true` reproduces that, and is also the only lever
	# on the probe field's scale — see `docs/adr_pending_182.md` for what the two
	# settings do to the two consumers.
	_lightmap.camera_attributes = (
		LookEnv.camera_attributes(_args)
		if Cmdline.as_bool(_args, "bake_camera_attributes", false) else null
	)


func _bounds() -> AABB:
	var box := AABB()
	var started := false
	for point in _track.centerline(0.2, 8.0):
		if not started:
			box = AABB(point, Vector3.ZERO)
			started = true
		else:
			box = box.expand(point)
	return box


# --- run time --------------------------------------------------------------


## A parked camera, so `shoot.sh` can photograph the bake without the driveable
## scene's rig. `--eye`/`--look` as everywhere else.
func _build_camera() -> void:
	var camera := Camera3D.new()
	camera.name = "BakeCamera"
	camera.fov = Cmdline.as_float(_args, "fov", 55.0)
	camera.near = 0.05
	camera.far = 2500.0
	camera.attributes = LookEnv.camera_attributes(_args)
	add_child(camera)
	var eye := _parse_point(Cmdline.as_string(_args, "eye", ""), Vector3(26.0, 7.0, 34.0))
	var target := _parse_point(Cmdline.as_string(_args, "look", ""), Vector3(-6.0, 1.5, -90.0))
	var up := Vector3.UP
	if absf((target - eye).normalized().dot(Vector3.UP)) > 0.999:
		up = Vector3.FORWARD
	camera.look_at_from_position(eye, target, up)
	camera.current = true


func _parse_point(text: String, fallback: Vector3) -> Vector3:
	if text == "":
		return fallback
	var parts := text.split(",")
	if parts.size() != 3:
		return fallback
	return Vector3(float(parts[0]), float(parts[1]), float(parts[2]))


func _report() -> void:
	var users := 0
	if _lightmap != null and _lightmap.light_data != null:
		users = _lightmap.light_data.get_user_count()
	print("circuit_bake: %s, %s layout, lightmap %s" % [
		_track.track_name(), _track.layout(),
		"%d users" % users if users > 0 else "NOT BAKED — run tools/bake/bake.sh",
	])
	for key in _sizes:
		print("  %-14s %s" % [key, _sizes[key]])


func _run_editor_bake() -> void:
	# Loaded rather than preloaded: it talks to `EditorInterface`, which does not
	# exist in an exported build, and a preload would make this script fail to
	# parse there.
	var driver: GDScript = load("res://tools/bake/editor_bake.gd")
	driver.bake_and_quit(self, _lightmap, BAKE_PATH)
