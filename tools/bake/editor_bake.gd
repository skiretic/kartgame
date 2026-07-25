@tool
extends RefCounted

## Drives a `LightmapGI` bake from a script, by reaching into the editor.
##
## **`LightmapGI.bake()` is not exposed to scripting in Godot 4.7.** It exists in
## C++ and the editor calls it, but it is never registered with `ClassDB`, so
## there is no `bake()` on the node from GDScript, from a `@tool` script, from an
## `EditorScript`, or from a GDExtension. `ClassDB.class_get_method_list(
## "LightmapGI")` returns forty-odd property accessors and nothing else. There is
## also no command-line option for it — `--export-*`, `--import` and `--doctool`
## are the only standalone editor tools Godot ships. Baking is a GUI operation.
##
## What *is* reachable is the editor plugin that owns the GUI operation.
## `LightmapGIEditorPlugin` keeps an `EditorFileDialog` as a child of its "Bake
## Lightmaps" toolbar button and connects that dialog's `file_selected` signal to
## the function that performs the bake. Emitting the signal calls the function.
## So this script finds the button, selects the `LightmapGI` node so the plugin
## knows what to bake, and emits `file_selected` with the output path — which is
## the same call the GUI makes, minus the file browser.
##
## Be clear about what this is: it drives editor internals that carry no
## compatibility promise, not an API. It is here because an automated bake is
## worth having and the alternative is a wiki page of click instructions that
## rots. If a Godot version moves that button, this breaks loudly and visibly,
## and `docs`-level instructions for doing it by hand are in `bake.sh`.
##
## Run through `tools/bake/bake.sh`, never directly.

## The editor is still assembling itself when the opened scene's `_ready()` runs,
## so the toolbar may not exist yet. Poll rather than guess a frame count.
const FIND_BUTTON_TIMEOUT_FRAMES := 240


static func bake_and_quit(context: Node, lightmap: LightmapGI, bake_path: String) -> void:
	var tree := context.get_tree()

	if lightmap == null:
		push_error("editor_bake: the scene has no LightmapGI node")
		tree.quit(2)
		return

	# The bake walks up from the LightmapGI node's parent, and the plugin refuses
	# to run at all unless the node it is baking belongs to the scene currently
	# open in the editor.
	if EditorInterface.get_edited_scene_root() != context:
		push_error("editor_bake: %s is not the edited scene root" % context.name)
		tree.quit(2)
		return

	var button := await _find_bake_button(tree)
	if button == null:
		push_error(
			"editor_bake: no LightmapGI bake button in the editor toolbar. "
			+ "Either the editor did not finish starting, or Godot moved it."
		)
		tree.quit(2)
		return

	if button.disabled:
		push_error("editor_bake: the bake button is disabled — %s" % button.tooltip_text)
		tree.quit(2)
		return

	# Selecting the node is what makes the plugin's `edit()` run, which is the
	# only way it learns which LightmapGI to bake. Without it the button press is
	# a no-op and the failure is completely silent.
	EditorInterface.edit_node(lightmap)
	await tree.process_frame
	await tree.process_frame

	var dialog := _bake_file_dialog(button)
	var adopted := _adopt_generated_nodes(context)
	print("editor_bake: baking %s -> %s (%d generated nodes adopted)" % [
		context.scene_file_path, bake_path, adopted.size(),
	])

	var started := Time.get_ticks_msec()
	# Synchronous. The bake blocks here, pumping the editor's progress dialog,
	# for as long as it takes.
	dialog.emit_signal("file_selected", bake_path)
	var elapsed_ms := Time.get_ticks_msec() - started

	_release_generated_nodes(adopted)

	if lightmap.light_data == null:
		push_error("editor_bake: the bake produced no data after %.1f s" % (elapsed_ms / 1000.0))
		# Every one of LightmapGI's twelve bake errors is reported by popping an
		# editor dialog and nothing else — no return value a caller can see, no
		# line in the log. Reading the dialog back out is the only way an
		# automated bake can say why it failed instead of just that it did.
		var reason := _warning_dialog_text(tree)
		if not reason.is_empty():
			push_error("editor_bake: the editor's reason was — %s" % reason)
		_report_meshes(context)
		tree.quit(1)
		return

	print("editor_bake: baked in %.1f s, %d mesh users" % [
		elapsed_ms / 1000.0, lightmap.light_data.get_user_count(),
	])
	_report_textures(lightmap)

	# The bake sets `light_data` on the node. Unsaved, the scene still has no
	# lightmap the next time anything opens it, and the whole run was for nothing.
	var save_error := EditorInterface.save_scene()
	if save_error != OK:
		push_error("editor_bake: save_scene() failed with %d" % save_error)
		tree.quit(1)
		return

	print("editor_bake: saved %s" % context.scene_file_path)
	tree.quit(0)


## Finds the plugin's bake button without depending on its label.
##
## Matching on the text "Bake Lightmaps" would break under any editor locale.
## The button is identified structurally instead: it is the only control in the
## editor that owns an `EditorFileDialog` filtering for `*.lmbake`.
static func _find_bake_button(tree: SceneTree) -> Button:
	for frame in FIND_BUTTON_TIMEOUT_FRAMES:
		var found := _search_for_bake_button(EditorInterface.get_base_control())
		if found != null:
			return found
		await tree.process_frame
	return null


static func _search_for_bake_button(node: Node) -> Button:
	var button := node as Button
	if button != null and _bake_file_dialog(button) != null:
		return button
	for child in node.get_children():
		var found := _search_for_bake_button(child)
		if found != null:
			return found
	return null


static func _bake_file_dialog(button: Button) -> Node:
	for child in button.get_children():
		if not child.is_class("EditorFileDialog"):
			continue
		if String("|").join(child.get("filters")).contains("lmbake"):
			return child
	return null


## Makes the scene's generated geometry visible to the bake, temporarily.
##
## `LightmapGI::_find_meshes_and_lights` walks the tree and skips any child whose
## `owner` is null — the engine reads an unowned node as an editor helper rather
## than as scene content. Everything a `@tool` script builds at run time is
## unowned, so a code-built scene is invisible to the baker and reports the same
## "no meshes with lightmapping support" as a scene whose meshes genuinely have
## no UV2. Nothing distinguishes the two from the error message, and the meshes
## look perfectly correct under inspection, which is what makes it expensive.
##
## Adopting them fixes it, and is then undone before the scene is saved: an owned
## node is a *serialized* node, and leaving them adopted would write every
## generated wall into the .tscn and build a second copy of the scene on the next
## open. This matters well past this test scene — the M5 track is generated the
## same way.
static func _adopt_generated_nodes(root: Node) -> Array[Node]:
	var adopted: Array[Node] = []
	_adopt_descendants(root, root, adopted)
	return adopted


static func _adopt_descendants(root: Node, node: Node, adopted: Array[Node]) -> void:
	for child in node.get_children():
		if child.owner == null:
			child.owner = root
			adopted.append(child)
		_adopt_descendants(root, child, adopted)


static func _release_generated_nodes(adopted: Array[Node]) -> void:
	for node in adopted:
		node.owner = null


static func _warning_dialog_text(tree: SceneTree) -> String:
	return _search_for_dialog_text(tree.root)


static func _search_for_dialog_text(node: Node) -> String:
	if node.is_class("AcceptDialog") and node.visible:
		return String(node.get("dialog_text")).strip_edges()
	for child in node.get_children():
		var text := _search_for_dialog_text(child)
		if not text.is_empty():
			return text
	return ""


## The census the failing bake did not print.
##
## `BAKE_ERROR_NO_MESHES` is one error covering four causes, so when it fires the
## next question is always which of them, and for which node.
static func _report_meshes(root: Node) -> void:
	for node in root.find_children("*", "MeshInstance3D", true, false):
		var instance := node as MeshInstance3D
		var uv2 := false
		if instance.mesh != null and instance.mesh.get_surface_count() > 0:
			uv2 = instance.mesh.surface_get_arrays(0)[Mesh.ARRAY_TEX_UV2] != null
		print("editor_bake:   %-24s gi_mode %d  uv2 %s  visible %s" % [
			root.get_path_to(instance), instance.gi_mode, uv2, instance.is_visible_in_tree(),
		])


## What actually landed on disk, so the run is verifiable from its own log.
##
## The `.lmbake` holds the `LightmapGIData` resource — users, probes, capture
## data. The pixels are beside it in a `.exr` imported as a
## `CompressedTexture2DArray`: every mesh gets a rectangle in an atlas page, and
## a new slice is only started when the meshes stop fitting in the current one.
static func _report_textures(lightmap: LightmapGI) -> void:
	for texture in lightmap.light_data.get_lightmap_textures():
		var layered := texture as TextureLayered
		if layered == null:
			continue
		print("editor_bake: lightmap %dx%d, %d slices, %s" % [
			layered.get_width(), layered.get_height(), layered.get_layers(),
			layered.resource_path,
		])
