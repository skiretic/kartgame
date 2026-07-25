class_name PhysicsDraw
extends MeshInstance3D

## ROADMAP M3a's physics debug visualization: suspension rays, contact points,
## contact normals, the center of mass, and the velocity vector.
##
## Toggled with `debug_physics_draw` (F11).
##
## Godot's own `--debug-collisions` draws collision *shapes*, which is the one
## thing here that is not interesting: a box is a box. What is worth seeing is the
## state the vehicle model is computing — which wheels believe they are on the
## ground, where, and how hard they are sliding. That is invisible in every other
## way, and at M3b it is the difference between "the kart feels wrong" and "the
## inside rear is losing contact 40 ms before it should".
##
## Everything is drawn in world space from one `ImmediateMesh` rebuilt each frame.
## At five objects' worth of lines that is far cheaper than keeping a node per
## marker in sync, and it cannot leave a stale marker behind after a respawn.

const COLOR_CONTACT := Color(0.25, 0.95, 0.35)
const COLOR_AIRBORNE := Color(0.95, 0.30, 0.25)
const COLOR_NORMAL := Color(0.35, 0.70, 1.00)
const COLOR_VELOCITY := Color(1.00, 0.85, 0.20)
const COLOR_CENTER_OF_MASS := Color(1.00, 0.40, 0.90)

## Meters of line per m/s of velocity. 0.1 keeps the vector inside the frame at
## 140 km/h instead of drawing a 39 m spear off the screen.
const VELOCITY_SCALE := 0.1

## Half-size of the cross drawn at a contact point and at the center of mass.
const MARKER_SIZE := 0.06

var _kart: KartDebugVehicle
var _mesh: ImmediateMesh


func _ready() -> void:
	_mesh = ImmediateMesh.new()
	mesh = _mesh
	# World space, so the mesh must not inherit the kart's transform even though
	# it is parented under the scene root next to it.
	top_level = true
	# Unshaded and depth-tested off: a debug line behind the chassis is still a
	# line you need to see. `no_depth_test` is what makes this readable at all
	# when the interesting wheel is the far one.
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.vertex_color_use_as_albedo = true
	material.no_depth_test = true
	material.disable_receive_shadows = true
	material_override = material
	visible = false


func set_target(kart: KartDebugVehicle) -> void:
	_kart = kart


func _process(_delta: float) -> void:
	if Input.is_action_just_pressed(&"debug_physics_draw"):
		visible = not visible
	if not visible or _kart == null:
		return
	_rebuild()


func _rebuild() -> void:
	_mesh.clear_surfaces()
	_mesh.surface_begin(Mesh.PRIMITIVE_LINES)

	for wheel: Dictionary in _kart.wheel_report():
		var origin: Vector3 = wheel["origin"]
		var color: Color = COLOR_CONTACT if wheel["contact"] else COLOR_AIRBORNE
		# The suspension ray, drawn from the wheel node down through its travel.
		# A red ray is a wheel in the air, which on a kart in a corner is the
		# defining behavior ARCHITECTURE.md §6 is built around — so it is worth
		# being able to see the exact moment it happens.
		_line(origin, origin + Vector3.DOWN * float(wheel["travel"]), color)

		if not wheel["contact"]:
			continue
		# `VehicleWheel3D` does not publish its contact point, so it is
		# reconstructed: the wheel hangs its radius below the node, less whatever
		# the suspension has compressed. Approximate on a slope and exact on a
		# plane, which is the accuracy this milestone's ground has.
		var contact := origin + Vector3.DOWN * float(wheel["radius"])
		_cross(contact, MARKER_SIZE, color)
		_line(contact, contact + Vector3.UP * 0.25, COLOR_NORMAL)

		# Skid, drawn as a lateral bar whose length is how much grip is gone.
		# `get_skidinfo` returns 1.0 for a wheel with full grip and falls toward 0
		# as it slides.
		var slide := 1.0 - clampf(float(wheel["skid"]), 0.0, 1.0)
		if slide > 0.01:
			var right := _kart.global_transform.basis.x * slide * 0.4
			_line(contact - right, contact + right, COLOR_AIRBORNE)

	var center := _kart.to_global(_kart.center_of_mass)
	_cross(center, MARKER_SIZE * 1.6, COLOR_CENTER_OF_MASS)
	_line(center, center + _kart.linear_velocity * VELOCITY_SCALE, COLOR_VELOCITY)

	_mesh.surface_end()


func _line(from: Vector3, to: Vector3, color: Color) -> void:
	_mesh.surface_set_color(color)
	_mesh.surface_add_vertex(from)
	_mesh.surface_set_color(color)
	_mesh.surface_add_vertex(to)


func _cross(at: Vector3, size: float, color: Color) -> void:
	_line(at - Vector3.RIGHT * size, at + Vector3.RIGHT * size, color)
	_line(at - Vector3.UP * size, at + Vector3.UP * size, color)
	_line(at - Vector3.BACK * size, at + Vector3.BACK * size, color)
