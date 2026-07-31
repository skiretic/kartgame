extends Node3D

## Issue #22 — the kart turntable. The reason geometry generation moved to
## Blender was partly that output can be *looked at* rather than debugged blind
## (ADR-0012), and this is that.
##
##     tools/blender/genkart.sh
##     tools/shots/shoot.sh --scene=res://scenes/look/kartview.tscn \
##                          --turntable=9 --resolution=640x640 \
##                          --out=shots/kart-turntable.png
##
## Arguments (all optional, all after a bare `--`):
##
##   --kart=res://assets/generated/kart.glb   what to show
##   --views=canonical    canonical (9 chosen angles) or orbit (even yaw steps)
##   --ground=checker     checker (1 m squares, the scale read) or asphalt
##   --reference=true     a 1 m cube beside the kart
##   --margin=1.18        how much room around the kart in frame
##   --taa=false          off by default here, see the note on determinism
##
## plus every lighting and exposure argument `LookEnv` understands, because the
## lighting is not this scene's to invent — see `scripts/look/look_env.gd` for
## why that matters.
##
## ## Nine chosen angles rather than an even orbit
##
## `--views=canonical` is the default because an even orbit spends most of its
## frames on views that answer nothing. What actually catches a wrong kart is a
## specific list: the 3/4 front for overall proportion, a true side for
## wheelbase and rake, a grazing low view for ground clearance, a top view for
## the frame's plan-view pinch, and the cockpit view for what the driver sees.
## `--views=orbit` still exists for a smooth spin.
##
## ## On determinism
##
## The M2 gate requires that identical parameters produce an identical *mesh*,
## and they do — byte-identically, checked by `genkart.sh --check`. Issue #22
## additionally asks for identical *images*, and that turns out not to be
## available: Godot stills on this host are usually byte-identical but not
## reliably so, drifting by up to 18/255 on about half the frame with no input
## change (ADR-0023). So this scene turns the temporal effects off by default,
## which removes most of the variance, and the image check uses a tolerance
## rather than a hash. The geometry review does not need TAA; judging silhouette
## and scale is not a question about temporal resolve.

## Padding around the kart's bounding sphere. Just over 1 so the kart does not
## touch the frame edge, which makes a clipped part look intentional.
const DEFAULT_MARGIN := 1.18

## Ground plane extent. Small compared with the look-dev scene's 2 km: this scene
## looks *at* an object rather than down a road, so a large plane only costs fill
## rate and pushes the horizon out of frame anyway.
const GROUND_SIZE := 60.0

## (name, yaw degrees, pitch degrees, distance scale)
##
## Yaw 0 puts the camera off the kart's nose; 90 is the kart's right-hand side,
## which is the side a KZ carries its engine and exhaust on and therefore the
## more informative flank.
const CANONICAL_VIEWS: Array = [
	["3/4 front", 35.0, 18.0, 1.0],
	["side", 90.0, 8.0, 1.0],
	["3/4 rear", 145.0, 18.0, 1.0],
	["front", 0.0, 10.0, 1.0],
	["top", 20.0, 78.0, 1.05],
	["rear", 180.0, 10.0, 1.0],
	["low front", 25.0, 2.0, 0.92],
	["frame detail", 62.0, 30.0, 0.48],
	["cockpit", 0.0, 0.0, 0.0],
]

var _camera: Camera3D
var _caption: Label
var _kart: Node3D
var _bounds := AABB()
var _args: Dictionary = {}
var _view_mode := "canonical"
var _margin := DEFAULT_MARGIN
var _manifest: Dictionary = {}
var _measured := ""

## The view last asked for, held so a request that arrives before `_ready` is not
## lost. `tools/shots/shoot.gd` calls `set_turntable_view(0, N)` from
## `_initialize`, which runs before this node's `_ready` — the camera does not
## exist yet, so that call returns early and `_ready` used to overwrite the count
## with 1. The first cell of every contact sheet was captioned "view 1/1" as a
## result, which is a small lie in the one place a still is supposed to describe
## itself completely.
var _requested_index := 0
var _requested_count := 1


func _ready() -> void:
	_args = Cmdline.parse()
	_view_mode = Cmdline.as_string(_args, "views", "canonical")
	_margin = Cmdline.as_float(_args, "margin", DEFAULT_MARGIN)

	_build_environment()
	add_child(LookEnv.sun(_args))
	_build_ground()
	_load_manifest()
	_load_kart()
	_build_reference_geometry()
	_build_probe()
	_build_camera()
	_build_caption()
	set_turntable_view(_requested_index, _requested_count)


# --- construction ----------------------------------------------------------


func _build_environment() -> void:
	var world_environment := WorldEnvironment.new()
	world_environment.environment = LookEnv.environment(_args)
	add_child(world_environment)


func _build_ground() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(GROUND_SIZE, GROUND_SIZE)
	plane.subdivide_width = 24
	plane.subdivide_depth = 24

	var ground := MeshInstance3D.new()
	ground.mesh = plane
	if Cmdline.as_string(_args, "ground", "checker") == "asphalt":
		ground.material_override = LookEnv.asphalt_material(GROUND_SIZE)
	else:
		ground.material_override = LookEnv.checker_material(GROUND_SIZE)
	ground.name = "Ground"
	add_child(ground)


## The kart's own manifest, which is how the parameter block crosses languages.
##
## `genkart.py` writes every parameter it built from into a JSON sidecar. Reading
## it here rather than restating the dimensions in GDScript is the point: a kart
## whose seat moved would otherwise still be framed and captioned against the old
## number, and ARCHITECTURE.md §5 item 1 makes scale the thing most worth not
## getting wrong twice.
func _load_manifest() -> void:
	var path := Cmdline.as_string(_args, "manifest", "res://assets/generated/kart.json")
	if not FileAccess.file_exists(path):
		return
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed is Dictionary:
		_manifest = parsed


func _parameter(name: String, fallback: float) -> float:
	var parameters: Variant = _manifest.get("parameters", {})
	if parameters is Dictionary and parameters.has(name):
		return float(parameters[name])
	return fallback


func _load_kart() -> void:
	var path := Cmdline.as_string(_args, "kart", "res://assets/generated/kart.glb")
	if not ResourceLoader.exists(path):
		push_warning("kartview: %s does not exist — run tools/blender/genkart.sh first" % path)
		_measured = "no kart at %s" % path
		return

	var packed: PackedScene = load(path)
	if packed == null:
		push_warning("kartview: could not load %s" % path)
		_measured = "could not load %s" % path
		return

	_kart = packed.instantiate()
	_kart.name = "Kart"
	add_child(_kart)
	_bounds = _visual_bounds(_kart)
	_measured = _measure()


## Union of every visual instance's world-space AABB.
##
## `VisualInstance3D.get_aabb()` is in local space, so it is transformed before
## being merged. Empties contribute nothing, which is correct — a wheel pivot is
## not part of the kart's extent.
func _visual_bounds(node: Node) -> AABB:
	var bounds := AABB()
	var started := false
	for child in _walk(node):
		var visual := child as VisualInstance3D
		if visual == null:
			continue
		var local := visual.get_aabb()
		if local.size == Vector3.ZERO:
			continue
		var world := visual.global_transform * local
		if not started:
			bounds = world
			started = true
		else:
			bounds = bounds.merge(world)
	return bounds


func _walk(node: Node) -> Array[Node]:
	var found: Array[Node] = [node]
	for child in node.get_children():
		found.append_array(_walk(child))
	return found


## What the kart actually measures, in Godot, after the glTF round trip.
##
## This is issue #21's acceptance criterion rendered onto the image. The kart is
## built in Blender in meters and exported Y-up, and every step of that is a
## chance for a factor of 100 or a swapped axis to creep in — so the number that
## matters is the one measured here, not the one the parameter block asked for.
## Reporting both, with the delta, means a scale error is visible in the picture
## rather than discovered in M3b when the kart is the size of a bus.
func _measure() -> String:
	if _kart == null:
		return _measured

	var expected_length := _parameter("length_overall", 1.83)
	var expected_width := _parameter("track_rear", 1.40)

	var lines: Array[String] = []
	# Kart forward is -Z, so length is the Z extent and width the X extent.
	lines.append("measured  L %.3f m (want %.3f)   W %.3f m (want %.3f)   H %.3f m" % [
		_bounds.size.z, expected_length, _bounds.size.x, expected_width, _bounds.size.y,
	])

	var wheelbase := _measured_wheelbase()
	if wheelbase > 0.0:
		var expected := _parameter("wheelbase", 1.05)
		lines.append("wheelbase %.4f m (want %.4f, delta %+.4f)" % [
			wheelbase, expected, wheelbase - expected,
		])
	else:
		lines.append("wheelbase not measurable yet — no wheel pivots in the mesh")

	return "\n".join(lines)


## Wheelbase from the wheel pivot nodes, once issue #14 has published them.
##
## Measured between the front and rear pivots rather than from the bounding box,
## because a bounding box includes the bumpers and would report the overall
## length instead. The pivots are named by `kartlib`, and that name is an
## interface the vehicle solver will use in M3b for the same reason.
func _measured_wheelbase() -> float:
	if _kart == null:
		return 0.0
	var front := 0.0
	var rear := 0.0
	var found_front := false
	var found_rear := false
	for node in _walk(_kart):
		var spatial := node as Node3D
		if spatial == null:
			continue
		var name := spatial.name.to_lower()
		if not name.begins_with("wheel_"):
			continue
		var z := spatial.global_position.z
		if name.contains("f"):
			front = z
			found_front = true
		elif name.contains("r"):
			rear = z
			found_rear = true
	if not (found_front and found_rear):
		return 0.0
	return absf(rear - front)


## A 1 m cube beside the kart, which is the scale check M1 established.
##
## ROADMAP M1 accepted its look-dev scene against a 1 m reference cube, and
## issue #21 says to check the kart against it. A cube alone proves only that the
## cube is right; a cube next to a kart is what makes a wrong kart obvious,
## because a kart is a shade over one cube wide and just under two long.
func _build_reference_geometry() -> void:
	if not Cmdline.as_bool(_args, "reference", true):
		return
	var cube := MeshInstance3D.new()
	cube.mesh = BoxMesh.new()
	(cube.mesh as BoxMesh).size = Vector3.ONE
	# Parked at a yaw no canonical view looks from. At the kart's right the cube
	# sat exactly between the camera and the kart in the side view and filled the
	# whole cell. The canonical yaws are 0, 20, 25, 35, 62, 90, 145 and 180, so
	# 290 degrees is clear of all of them while staying in frame at the edge of
	# most, which is what makes it useful as a reference at all.
	var yaw := deg_to_rad(290.0)
	cube.position = Vector3(sin(yaw) * 1.7, 0.5, -cos(yaw) * 1.7)
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.55, 0.16, 0.13)
	material.roughness = 0.6
	cube.material_override = material
	cube.name = "ReferenceCube1m"
	add_child(cube)


## A probe scaled to the kart rather than to a road.
##
## The size matters: `LookEnv`'s default box is 80 x 20 x 120 m for the look-dev
## road, and a capture that large resolves nothing of a 1.8 m object. Its ambient
## is disabled either way, which is the part that has bitten this project twice.
func _build_probe() -> void:
	if not Cmdline.as_bool(_args, "probe", true):
		return
	add_child(LookEnv.reflection_probe(
		Vector3(6.0, 4.0, 6.0), Vector3(0.0, 1.2, 0.0), 20.0))


func _build_camera() -> void:
	_camera = Camera3D.new()
	_camera.fov = Cmdline.as_float(_args, "fov", 45.0)
	# Tighter than the look-dev scene's 70. A wide lens on a small object
	# exaggerates its nearest corner, and judging proportion through a distorted
	# lens is judging the lens.
	_camera.near = 0.05
	_camera.far = 200.0
	_camera.name = "TurntableCamera"
	_camera.attributes = LookEnv.camera_attributes(_args)
	add_child(_camera)

	var viewport := get_viewport()
	viewport.msaa_3d = Viewport.MSAA_DISABLED
	viewport.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
	viewport.scaling_3d_scale = 1.0
	# Off by default here, unlike the look-dev scene. See the determinism note in
	# this file's header: temporal accumulation is where the run-to-run drift
	# comes from, and a geometry review does not need it.
	viewport.use_taa = Cmdline.as_bool(_args, "taa", false)


func _build_caption() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)

	_caption = Label.new()
	_caption.add_theme_font_size_override("font_size", 15)
	_caption.add_theme_color_override("font_color", Color.WHITE)
	_caption.add_theme_color_override("font_outline_color", Color.BLACK)
	_caption.add_theme_constant_override("outline_size", 5)
	_caption.position = Vector2(14, 12)
	layer.add_child(_caption)


# --- views -----------------------------------------------------------------


## Called by `tools/shots/shoot.gd` once per contact-sheet cell.
##
## The whole view is set from scratch each time rather than being nudged from the
## previous one, so a cell's content depends only on its index — which is what
## makes a sheet comparable with the sheet before it.
func set_turntable_view(index: int, count: int) -> void:
	# Recorded before the guard, so a call that arrives ahead of `_ready` is
	# replayed rather than dropped. See `_requested_index`.
	_requested_index = index
	_requested_count = count
	if _camera == null:
		return

	var target := _bounds.get_center()
	if _bounds.size == Vector3.ZERO:
		target = Vector3(0.0, 0.4, 0.0)

	var name := ""
	var yaw := 0.0
	var pitch := 0.0
	var scale := 1.0

	if _view_mode == "orbit" or CANONICAL_VIEWS.size() == 0:
		name = "orbit"
		yaw = 360.0 * float(index) / float(maxi(count, 1))
		pitch = Cmdline.as_float(_args, "view_elevation", 16.0)
	else:
		var view: Array = CANONICAL_VIEWS[_canonical_index(index)]
		name = view[0]
		yaw = view[1]
		pitch = view[2]
		scale = view[3]

	# `--yaw` / `--pitch` / `--scale` override whichever view was selected, so a
	# part can be looked at from an angle no canonical view covers without
	# editing this file. Issue #116 needed the engine at 0.2 of the kart's
	# framing distance and no canonical view is closer than 0.48; the rule that a
	# still is reproducible from its command means the answer is an argument.
	yaw = Cmdline.as_float(_args, "yaw", yaw)
	pitch = Cmdline.as_float(_args, "pitch", pitch)
	scale = Cmdline.as_float(_args, "scale", scale)

	# And `--at=x,y,z` aims the orbit somewhere other than the kart's center, in
	# the kart's own coordinates. Judging a casting means filling the frame with
	# it, and the engine is 0.32 m off the centerline.
	var aim := Cmdline.as_string(_args, "at", "")
	if aim != "":
		var parts := aim.split(",")
		if parts.size() == 3:
			target = Vector3(
				float(parts[0]), float(parts[1]), float(parts[2]))
		else:
			push_warning("--at wants three comma-separated numbers, got: " + aim)

	if name == "cockpit" and not _args.has("yaw"):
		_place_cockpit()
	else:
		_place_orbit(target, yaw, pitch, scale)

	_caption.text = "\n".join([
		"view %d/%d  %s   yaw %.0f  pitch %.0f" % [index + 1, count, name, yaw, pitch],
		_measured,
		_mesh_line(),
		LookEnv.exposure_caption(_args),
	])


## Which canonical view a turntable cell shows.
##
## `--view=<name or index>` pins every cell to one view, which is what makes a
## single close-up renderable: `--turntable=1 --view="frame detail"` is one cell
## rather than the first of nine. Without it the only way to a chosen angle was
## to render all nine and crop, which throws away eight ninths of the pixels.
func _canonical_index(index: int) -> int:
	var pinned := Cmdline.as_string(_args, "view", "")
	if pinned == "":
		return index % CANONICAL_VIEWS.size()
	if pinned.is_valid_int():
		return clampi(pinned.to_int(), 0, CANONICAL_VIEWS.size() - 1)
	for candidate in range(CANONICAL_VIEWS.size()):
		if String(CANONICAL_VIEWS[candidate][0]) == pinned:
			return candidate
	push_warning("--view names no canonical view: " + pinned)
	return index % CANONICAL_VIEWS.size()


func _place_orbit(target: Vector3, yaw: float, pitch: float, scale: float) -> void:
	# Fit the bounding sphere to the vertical field of view. Using the sphere
	# rather than the box means the framing does not change as the kart rotates,
	# so two cells are the same size and can be compared directly.
	var radius := maxf(_bounds.size.length() * 0.5, 0.35)
	var half_fov := deg_to_rad(_camera.fov) * 0.5
	var distance := radius / maxf(sin(half_fov), 0.01) * _margin * scale

	var yaw_radians := deg_to_rad(yaw)
	var pitch_radians := deg_to_rad(pitch)
	# Yaw 0 sits off the nose. The kart faces -Z, so that is the -Z side.
	var offset := Vector3(
		sin(yaw_radians) * cos(pitch_radians),
		sin(pitch_radians),
		-cos(yaw_radians) * cos(pitch_radians)) * distance

	_camera.position = target + offset
	_camera.look_at(target, Vector3.UP)


## The driver's eye, looking forward.
##
## The eye is *derived from the seat*, not offset from it by a chosen number. The
## first version of this put the eye 0.10 m forward of the hip point, which put
## the camera inside the steering wheel — a kart seat is reclined hard, so the
## head ends up behind the hip, not ahead of it.
##
## The derivation, entirely from the parameter block:
##
##   * the hip is at (`seat_y`, `seat_z`), which is what `cockpit.py` builds the
##     seat's spine around;
##   * the torso lies along the seat back, reclined `seat_back_angle` from
##     vertical, so the shoulder at `driver_shoulder_z` is that much *rearward*
##     of the hip;
##   * the head is carried upright above the shoulder — the neck is what
##     un-reclines it — so the eye at `driver_eye_z` sits a little forward of the
##     shoulder rather than continuing along the torso line.
##
## Every term is a manifest parameter except `NECK_CARRY`, and that one is here
## rather than in `params.py` because it describes a pose, not a dimension of the
## kart. M4's real cockpit rig replaces all of this; what it must not do is
## disagree with it about where the head is, which is why the numbers come from
## the same block the seat was built from.
const NECK_CARRY := 0.045

func _place_cockpit() -> void:
	var eye_height := _parameter("driver_eye_z", 0.62)
	var shoulder_height := _parameter("driver_shoulder_z", 0.47)
	var seat_y := _parameter("seat_y", -0.06)
	var seat_z := _parameter("seat_z", 0.075)
	var back_angle := _parameter("seat_back_angle", 0.61)

	var shoulder_y := seat_y - (shoulder_height - seat_z) * tan(back_angle)
	var eye_y := shoulder_y + NECK_CARRY
	# Blender +Y forward maps to Godot -Z, so a Blender y becomes a Godot -z.
	_camera.position = Vector3(0.0, eye_height, -eye_y)
	# Aimed down at the nose rather than level. A level cockpit camera at this
	# height sees only horizon and sky, because everything a kart driver looks at
	# on their own kart — the nose, the tray, their own hands — is below the eye
	# line. M4 sets the real rig's pitch; this only has to show the geometry.
	_camera.look_at(Vector3(0.0, 0.02, -1.35), Vector3.UP)


## Which mesh this sheet is actually showing.
##
## A still is supposed to be fully described by the command that produced it, and
## a turntable of generated geometry breaks that rule quietly — the command names
## a path, not a mesh. Printing the manifest's hash and counts closes it, the same
## problem issue #101 raises for baked stills.
func _mesh_line() -> String:
	if _manifest.is_empty():
		return "no manifest — dimensions above are unverified against parameters"
	var totals: Variant = _manifest.get("totals", {})
	var digest := str(_manifest.get("sha256", "")).substr(0, 12)
	var vertices := 0
	var triangles := 0
	if totals is Dictionary:
		vertices = int(totals.get("vertices", 0))
		triangles = int(totals.get("triangles", 0))
	var line := "mesh %s   %s verts  %s tris   blender %s" % [
		digest, vertices, triangles, _manifest.get("blender", "?"),
	]

	# The caption is only worth having if it describes the mesh actually on
	# screen, and it can silently stop doing that. `shoot.sh SKIP_IMPORT=1` skips
	# Godot's import step, so a regenerated `.glb` renders from the *previous*
	# import while this line reads the *new* manifest straight off disk — a fresh
	# hash printed over a stale kart. That happened, and it was only caught
	# because a deliberate geometry change produced a pixel-identical render,
	# which is not a signal anything should have to rely on.
	var built := _triangles_in_scene()
	if built > 0 and triangles > 0 and built != triangles:
		line += "\nSTALE IMPORT — %d tris on screen, manifest says %d. Re-run without SKIP_IMPORT=1." % [
			built, triangles,
		]

	# The texture half of the same trap, from the other direction: a
	# `--stages=...` iteration run that skips the bake exports a glb with zero
	# images over the full artifact, and the triangle count matches exactly —
	# so the caption above stays clean while every material lost its maps.
	# #210 measured it. The manifest records which textures the run produced;
	# if it names any and the loaded materials carry none, say so.
	var manifest_textures: Variant = _manifest.get("textures", {})
	if manifest_textures is Dictionary and not (manifest_textures as Dictionary).is_empty():
		if _textured_materials_in_scene() == 0:
			line += "\nTEXTURELESS GLB — manifest names %d texture(s), loaded materials carry none. Last writer skipped the bake (--stages without bake?)." % [
				(manifest_textures as Dictionary).size(),
			]
	return line


## Triangles actually in the loaded scene, as the renderer has them.
##
## Compared against the manifest's triangle count rather than its vertex count,
## because the two vertex counts are not the same quantity and never will be:
## Blender reports merged mesh vertices, and the glTF buffer splits them again at
## every UV and normal seam. Triangles survive the round trip exactly — the
## exporter triangulates, and Godot's importer neither adds nor removes any at
## LOD 0 — so it is the one number that must agree, which is what makes a
## disagreement unambiguous rather than a tolerance question.
## Materials in the loaded kart that carry any image texture. Zero with a
## texture-bearing manifest means the artifact and the manifest disagree.
func _textured_materials_in_scene() -> int:
	if _kart == null:
		return 0
	var count := 0
	for node in _walk(_kart):
		var instance := node as MeshInstance3D
		if instance == null or instance.mesh == null:
			continue
		for surface in instance.mesh.get_surface_count():
			var material := instance.mesh.surface_get_material(surface) as BaseMaterial3D
			if material == null:
				continue
			if material.albedo_texture != null or material.normal_texture != null:
				count += 1
	return count


func _triangles_in_scene() -> int:
	if _kart == null:
		return 0
	var total := 0
	for node in _walk(_kart):
		var instance := node as MeshInstance3D
		if instance == null or instance.mesh == null:
			continue
		var mesh := instance.mesh
		for surface in mesh.get_surface_count():
			if mesh.surface_get_primitive_type(surface) != Mesh.PRIMITIVE_TRIANGLES:
				continue
			var indices := 0
			var array_mesh := mesh as ArrayMesh
			if array_mesh != null:
				indices = array_mesh.surface_get_array_index_len(surface)
			if indices > 0:
				total += indices / 3
			else:
				total += mesh.surface_get_array_len(surface) / 3
	return total
