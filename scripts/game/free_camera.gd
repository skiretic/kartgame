class_name FreeCamera
extends Camera3D

## The debug flight camera from ARCHITECTURE.md §7, with the frustum-freeze
## caveat it turns out to need.
##
## Controls, active only while this camera is current:
##
##     W A S D        forward / strafe        (the drive keys, reused)
##     Q E            down / up
##     mouse          look, once the window is clicked
##     shift          x4 speed
##     F4             freeze / unfreeze the frustum   (debug_camera)
##
## ## What "frustum freeze" can and cannot mean here
##
## The point of freezing a frustum is to fly outside it and see what the renderer
## was culling. Godot has no way to do that: culling is computed per camera from
## the camera that is current, and there is no viewport-level override. So what
## this does is draw the frozen frustum's wireframe and hold its position, which
## answers "was that object inside the view volume" — the question that gets asked
## in practice — while occlusion culling and mesh LOD continue to follow the live
## camera. Stated rather than implied, because a debug tool that quietly answers a
## different question than its name suggests is worse than not having one.

const BASE_SPEED := 8.0
const BOOST_MULTIPLIER := 4.0
const MOUSE_SENSITIVITY := 0.0022

## How far down the frozen frustum is drawn, meters. The far plane is 800 m and a
## wireframe box that long is unreadable, so the drawing is truncated where the
## interesting geometry is.
const FROZEN_DRAW_DISTANCE := 60.0

var _yaw := 0.0
var _pitch := 0.0
var _frozen := false
var _frozen_transform := Transform3D()
var _frustum_draw: MeshInstance3D


func _ready() -> void:
	fov = 70.0
	near = 0.05
	far = 800.0

	_frustum_draw = MeshInstance3D.new()
	_frustum_draw.name = "FrozenFrustum"
	_frustum_draw.mesh = ImmediateMesh.new()
	# Drawn in world space, so it must not inherit this camera's motion.
	_frustum_draw.top_level = true
	_frustum_draw.visible = false
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color(1.0, 0.85, 0.2)
	material.vertex_color_use_as_albedo = true
	_frustum_draw.material_override = material
	add_child(_frustum_draw)


func take_over(from: Camera3D) -> void:
	if from != null:
		global_transform = from.global_transform
	_yaw = global_rotation.y
	_pitch = global_rotation.x
	current = true


func _unhandled_input(event: InputEvent) -> void:
	if not current:
		return
	var motion := event as InputEventMouseMotion
	if motion != null and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		_yaw -= motion.relative.x * MOUSE_SENSITIVITY
		_pitch = clampf(_pitch - motion.relative.y * MOUSE_SENSITIVITY, -1.5, 1.5)


## Cleared while a menu is over the session.
##
## **`set_process_input(false)` was not enough and that is measured.** The flight
## controls are `Input.get_action_strength` **polls in `_process`**, not events,
## so gating `_input` leaves every one of them live — and the poll reads
## `throttle`, `brake`, `steer_left` and `steer_right`, whose second bindings are
## the arrow keys, which are also `menu_up/down/left/right`. So with the pause
## menu open in free-camera mode, one press of Down both walked the selection and
## flew the camera backwards.
##
## `set_input_as_handled()` cannot fix it either: consuming an event does not
## touch the `Input` singleton. That is the same fact CLAUDE.md records for
## `KartBody`, and the reason `PlayerDriver.enabled` exists rather than an
## event-consuming overlay. This flag is the camera's equivalent of that lever.
var enabled := true


func _process(delta: float) -> void:
	if not current or not enabled:
		return

	if Input.is_action_just_pressed(&"debug_camera"):
		_toggle_freeze()

	global_rotation = Vector3(_pitch, _yaw, 0.0)

	var move := Vector3.ZERO
	move.z -= Input.get_action_strength(&"throttle")
	move.z += Input.get_action_strength(&"brake")
	move.x -= Input.get_action_strength(&"steer_left")
	move.x += Input.get_action_strength(&"steer_right")
	if Input.is_key_pressed(KEY_E):
		move.y += 1.0
	if Input.is_key_pressed(KEY_Q):
		move.y -= 1.0

	var speed := BASE_SPEED
	if Input.is_key_pressed(KEY_SHIFT):
		speed *= BOOST_MULTIPLIER
	global_position += (global_transform.basis * move) * speed * delta


func _toggle_freeze() -> void:
	_frozen = not _frozen
	_frustum_draw.visible = _frozen
	if not _frozen:
		return
	_frozen_transform = global_transform
	_draw_frustum(_frozen_transform)


## The frozen view volume as eight corners and twelve edges.
##
## Built from the projection the camera actually has rather than from remembered
## trigonometry: `get_frustum` would give the planes, but the corners are what a
## wireframe needs, and the half-height at a distance is `tan(fov/2) * distance`
## with `fov` vertical and the aspect coming from the viewport.
func _draw_frustum(where: Transform3D) -> void:
	var mesh := _frustum_draw.mesh as ImmediateMesh
	mesh.clear_surfaces()

	var aspect := float(get_viewport().get_visible_rect().size.aspect())
	var half_angle := deg_to_rad(fov) * 0.5

	var corners: Array[Vector3] = []
	for distance: float in [near, FROZEN_DRAW_DISTANCE]:
		var half_height := tan(half_angle) * distance
		var half_width := half_height * aspect
		for sign_x: float in [-1.0, 1.0]:
			for sign_y: float in [-1.0, 1.0]:
				corners.append(
					where * Vector3(sign_x * half_width, sign_y * half_height, -distance)
				)

	# Index pairs: the near quad, the far quad, and the four rays joining them.
	var edges := [
		[0, 1], [1, 3], [3, 2], [2, 0],
		[4, 5], [5, 7], [7, 6], [6, 4],
		[0, 4], [1, 5], [2, 6], [3, 7],
	]
	mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	for edge: Array in edges:
		mesh.surface_set_color(Color(1.0, 0.85, 0.2))
		mesh.surface_add_vertex(corners[edge[0]])
		mesh.surface_set_color(Color(1.0, 0.85, 0.2))
		mesh.surface_add_vertex(corners[edge[1]])
	mesh.surface_end()
