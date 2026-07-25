extends SceneTree

## Checks that a scene can be lightmapped, and what it will cost, without baking.
##
##     godot --headless --path . --script res://tools/bake/preflight.gd \
##           -- --scene=res://scenes/look/bake_test.tscn
##
## The bake itself needs the GUI editor (see `tools/bake/editor_bake.gd` for why),
## but every reason a bake *fails* can be established headlessly, and this is
## worth far more than it looks. `LightmapGI` reports one error —
## `BAKE_ERROR_NO_MESHES` — for four different mistakes: no `UV2`, no normals,
## `gi_mode` left on its `GI_MODE_DYNAMIC` default, or the mesh not visible. On a
## scene with one wall in it you find the cause in a minute. On a 1,500 m track
## with a few hundred instances, one instance silently missing `UV2` is
## indistinguishable from all of them missing it, and you will not find it by
## looking at the render.
##
## It also prices the atlas. `LightmapGI` sizes each mesh from its
## `lightmap_size_hint` — and a mesh with no hint is silently baked at 64x64
## regardless of how big it is, which on a track surface means a lightmap with
## roughly one texel per twenty meters and no visible error anywhere.
##
## Exits non-zero if anything in the scene would fail to bake.

## What `LightmapGI` falls back to when a mesh carries no size hint. From
## `LightmapGI::bake()`; the TODO next to it in the engine says a size "should"
## be computed and is not.
const NO_HINT_FALLBACK := Vector2i(64, 64)

var _scene_path: String
var _scene_root: Node


func _initialize() -> void:
	var args := Cmdline.parse()

	_scene_path = Cmdline.as_string(args, "scene", "")
	if _scene_path.is_empty():
		push_error("preflight: --scene=res://path/to.tscn is required")
		quit(2)
		return

	var packed: PackedScene = load(_scene_path)
	if packed == null:
		push_error("preflight: could not load %s" % _scene_path)
		quit(2)
		return

	_scene_root = packed.instantiate()
	root.add_child(_scene_root)


## Inspection waits a frame rather than running straight after `add_child`.
##
## `_ready()` does not fire during `add_child` from `_initialize()` — the tree is
## not iterating yet — so a scene that builds itself in `_ready()` is still empty
## at that point, and checking it there reports zero bakeable meshes for every
## scene in the project. Exactly the false negative this tool exists to prevent.
func _process(_delta: float) -> bool:
	# quit() in _initialize() schedules the exit, it does not take effect
	# immediately, so _process still runs once with nothing loaded. Without this
	# guard a missing --scene reports a null dereference on top of the real error,
	# which buries the message that would have told the caller what to fix.
	if _scene_root == null:
		return true

	var scene_root := _scene_root
	var scene_path := _scene_path
	var lightmap := _find_lightmap(scene_root)
	if lightmap == null:
		push_error("preflight: %s has no LightmapGI node, so nothing can be baked" % scene_path)
		quit(1)
		return true

	var texel_scale := lightmap.texel_scale
	var supersampling := lightmap.supersampling_factor if lightmap.supersampling else 1.0
	print("preflight: %s" % scene_path)
	print("  texel_scale %.2f   supersampling %.1fx   max_texture_size %d   bounces %d" % [
		texel_scale, supersampling, lightmap.max_texture_size, lightmap.bounces,
	])
	# The counter-intuitive direction, established by baking it both ways. See
	# _configure_lightmap in scripts/look/bake_test.gd: camera_attributes does
	# not set the exposure the bake works at, it sets the divisor the *renderer*
	# applies afterwards. Giving it the camera's own exposure makes that divisor
	# cancel the camera's normalization and the lightmap renders pure white.
	var physical: bool = ProjectSettings.get_setting(
		"rendering/lights_and_shadows/use_physical_light_units", false
	)
	if physical and lightmap.camera_attributes != null:
		push_warning(
			"preflight: physical light units are on and LightmapGI.camera_attributes "
			+ "is set. If those attributes match the camera's, every lightmapped "
			+ "surface will render blown out to white."
		)

	var failures := 0
	var total_texels := 0
	var candidates := 0
	var unowned := 0

	for node in _mesh_instances(scene_root):
		if node.owner == null:
			unowned += 1
		var reasons := _unbakeable_reasons(node)
		if not reasons.is_empty():
			# Only a complaint if it looks like it was meant to be baked. A
			# GI_MODE_DYNAMIC mesh with no UV2 is a kart, not a mistake.
			if node.gi_mode == GeometryInstance3D.GI_MODE_STATIC:
				push_error("preflight: %s cannot bake — %s" % [
					scene_root.get_path_to(node), ", ".join(reasons),
				])
				failures += 1
			continue

		candidates += 1
		var hint: Vector2i = node.mesh.lightmap_size_hint
		var hinted := hint != Vector2i.ZERO
		if not hinted:
			hint = NO_HINT_FALLBACK
			push_warning(
				"preflight: %s has no lightmap_size_hint, so it bakes at %dx%d "
				% [scene_root.get_path_to(node), hint.x, hint.y]
				+ "no matter how large it is"
			)

		var size := Vector2i(
			Vector2(hint) * node.gi_lightmap_texel_scale * texel_scale * supersampling
		)
		total_texels += size.x * size.y
		var extent: Vector3 = node.mesh.get_aabb().size * node.scale
		var longest := maxf(maxf(extent.x, extent.y), extent.z)
		print("  %-24s %5dx%-5d  %6.2f texel/m  %s" % [
			scene_root.get_path_to(node), size.x, size.y,
			maxi(size.x, size.y) / maxf(longest, 0.001),
			"" if hinted else "(no hint)",
		])

		if size.x > lightmap.max_texture_size or size.y > lightmap.max_texture_size:
			push_error(
				"preflight: %s needs a %dx%d slice, over the %d limit. "
				% [scene_root.get_path_to(node), size.x, size.y, lightmap.max_texture_size]
				+ "Split the mesh or lower its lightmap_texel_scale."
			)
			failures += 1

	print("  %d bakeable meshes, %.1f Mtexel before atlas packing" % [
		candidates, total_texels / 1_048_576.0,
	])

	if candidates == 0:
		push_error("preflight: nothing to bake — LightmapGI would report NO_MESHES")
		failures += 1

	# Not an error, because tools/bake/bake.sh adopts them for the duration of the
	# bake. It is a warning because a bake driven by hand from the editor does
	# not, and the bake then reports NO_MESHES on geometry that is provably
	# correct in every other respect — see _adopt_generated_nodes in
	# tools/bake/editor_bake.gd for what the engine is actually doing.
	if unowned > 0:
		push_warning(
			"preflight: %d of %d meshes have no owner, so LightmapGI will walk "
			% [unowned, candidates]
			+ "straight past them. bake.sh handles this; baking by hand from the "
			+ "editor does not."
		)

	quit(1 if failures > 0 else 0)
	return true


## Every reason `LightmapGI::_find_meshes_and_lights` would skip this instance.
##
## Reported together rather than one at a time, because they usually come in
## pairs — geometry authored without a lightmap UV set is normally also sitting
## on the default `gi_mode`.
func _unbakeable_reasons(node: MeshInstance3D) -> PackedStringArray:
	var reasons := PackedStringArray()

	if node.gi_mode != GeometryInstance3D.GI_MODE_STATIC:
		reasons.append("gi_mode is not GI_MODE_STATIC")
	if not node.is_visible_in_tree():
		reasons.append("not visible")
	if node.mesh == null:
		reasons.append("no mesh")
		return reasons

	# `surface_get_primitive_type` is only on ArrayMesh in the script API, not on
	# Mesh, so a primitive mesh is taken at its word — every PrimitiveMesh Godot
	# ships builds triangles.
	var array_mesh := node.mesh as ArrayMesh

	for surface in node.mesh.get_surface_count():
		if array_mesh != null:
			if array_mesh.surface_get_primitive_type(surface) != Mesh.PRIMITIVE_TRIANGLES:
				continue
		var arrays := node.mesh.surface_get_arrays(surface)
		if arrays[Mesh.ARRAY_TEX_UV2] == null:
			reasons.append("surface %d has no UV2" % surface)
		if arrays[Mesh.ARRAY_NORMAL] == null:
			reasons.append("surface %d has no normals" % surface)

	return reasons


func _find_lightmap(node: Node) -> LightmapGI:
	var lightmap := node as LightmapGI
	if lightmap != null:
		return lightmap
	for child in node.get_children():
		var found := _find_lightmap(child)
		if found != null:
			return found
	return null


func _mesh_instances(node: Node) -> Array[MeshInstance3D]:
	var found: Array[MeshInstance3D] = []
	var instance := node as MeshInstance3D
	if instance != null:
		found.append(instance)
	for child in node.get_children():
		found.append_array(_mesh_instances(child))
	return found
