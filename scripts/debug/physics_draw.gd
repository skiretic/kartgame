class_name PhysicsDraw
extends MeshInstance3D

## The physics debug visualization: suspension rays, contact points and normals,
## tire forces, friction-circle utilization, the center of mass and the velocity
## vector.
##
## Toggled with `debug_physics_draw` (F5). It was documented as F11 here for a
## milestone and F11 is bound to nothing at all — read off the InputMap rather
## than off memory if this ever looks wrong again.
##
## Godot's own `--debug-collisions` draws collision *shapes*, which is the one
## thing here that is not interesting: a box is a box. What is worth seeing is the
## state the vehicle model is computing — which wheels believe they are on the
## ground, where, how hard, and how much of the friction they have left. That is
## invisible in every other way, and it is the difference between "the kart feels
## wrong" and "the inside rear is losing contact 40 ms before it should".
##
## Everything is drawn in world space from one `ImmediateMesh` rebuilt each frame.
## At five objects' worth of lines that is far cheaper than keeping a node per
## marker in sync, and it cannot leave a stale marker behind after a respawn.
##
## ## Nothing here is reconstructed
##
## This closes issue #124, and the bug it closes is worth stating because it was
## invisible for a milestone. The old version drew each suspension ray along
## **world** down — `origin + Vector3.DOWN * travel` — so every ray it drew was
## wrong the moment the kart rolled, which is the only moment anybody turns this
## on. It also reconstructed the contact point from the wheel radius, because
## `VehicleWheel3D` publishes no contact point, so what was drawn was an
## approximation of what the sim was doing rather than the thing itself.
##
## `KartBody.wheel_report()` now serves the ray direction, the ray length, the
## contact point and the contact normal, all in world space and all taken from the
## same values the solver was handed. Every geometric quantity below is read, not
## derived. A debug view that computes its own answer can agree with itself while
## disagreeing with the simulation, which is the worst thing a debug view can do.

const COLOR_CONTACT := Color(0.25, 0.95, 0.35)
const COLOR_AIRBORNE := Color(0.95, 0.30, 0.25)
const COLOR_LATCHED := Color(1.00, 0.55, 0.10)
const COLOR_NORMAL := Color(0.35, 0.70, 1.00)
const COLOR_FORCE := Color(0.55, 0.45, 1.00)
const COLOR_SLIDING := Color(0.98, 0.75, 0.10)
const COLOR_VELOCITY := Color(1.00, 0.85, 0.20)
const COLOR_CENTER_OF_MASS := Color(1.00, 0.40, 0.90)

## Meters of line per m/s of velocity. 0.1 keeps the vector inside the frame at
## 140 km/h instead of drawing a 39 m spear off the screen.
const VELOCITY_SCALE := 0.1

## Meters of line per newton of tire force. One corner of this kart carries about
## 500 N statically and can produce a bit over 1 kN of grip, so 0.4 mm/N draws a
## hard-working tire at about half a meter — long enough to read a direction off,
## short enough that four of them do not fill the screen.
const FORCE_SCALE := 0.0004

## Half-size of the cross drawn at a contact point and at the center of mass.
const MARKER_SIZE := 0.06

## Half-length of the utilization bar at exactly 1.0, meters. Drawn across the
## kart, so it is readable from the chase camera at the moment a tire lets go.
const UTILIZATION_SCALE := 0.35

var _kart: KartBody
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


func set_target(kart: KartBody) -> void:
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
		_draw_wheel(wheel)

	var center := _kart.to_global(_kart.center_of_mass)
	_cross(center, MARKER_SIZE * 1.6, COLOR_CENTER_OF_MASS)
	_line(center, center + _kart.linear_velocity * VELOCITY_SCALE, COLOR_VELOCITY)

	_mesh.surface_end()


## One corner, entirely from what the boundary reported.
##
## Three states, three colors, and the third is the one that matters. A **latched**
## contact is a wheel whose suspension ray found nothing because the wheel is
## *buried* in the geometry rather than resting on it — `kart_body.h` convention 4:
## a ray that starts below a surface returns no hit at all, and the corner reports
## "no contact" exactly when it is most loaded. `KartBody` latches the last valid
## plane and keeps serving it, which is right, but a latched corner is being told
## about a surface that was measured on some earlier tick. It is drawn differently
## because it is a different fact, and because a corner that stays latched is
## either a curb strike or a wheel that has ended up inside the world.
func _draw_wheel(wheel: Dictionary) -> void:
	var origin: Vector3 = wheel["origin"]
	var direction: Vector3 = wheel["direction"]
	var length := float(wheel["length"])
	var contact := bool(wheel["contact"])
	var latched := bool(wheel["latched"])

	var color := COLOR_AIRBORNE
	if latched:
		color = COLOR_LATCHED
	elif contact:
		color = COLOR_CONTACT

	# The suspension ray, along the direction the boundary actually cast it —
	# chassis down, not world down. Issue #124.
	_line(origin, origin + direction * length, color)

	if not contact:
		return

	var point: Vector3 = wheel["point"]
	var normal: Vector3 = wheel["normal"]
	_cross(point, MARKER_SIZE, color)
	# The measured surface normal, not an assumed up. On a plane the two agree,
	# which is exactly why assuming it survived this long.
	_line(point, point + normal * 0.25, COLOR_NORMAL)

	if latched:
		# A second, larger cross so a latched corner is distinguishable at a glance
		# from a resting one even in a still, where the color is the only other cue
		# and stills get looked at in grayscale often enough.
		_cross(point, MARKER_SIZE * 2.2, COLOR_LATCHED)

	# The tire force this corner produced, in world newtons, drawn from its own
	# contact patch. Four of these is the entire cornering picture: which way each
	# tire is pushing and how hard.
	var force: Vector3 = wheel["force"]
	if force.length_squared() > 1.0:
		_line(point, point + force * FORCE_SCALE, COLOR_FORCE)

	# Utilization: the fraction of the friction ellipse this tire is using. Drawn
	# as a bar across the kart rather than as a number, because the thing being
	# watched for is the moment it crosses 1.0 — past that the tire is sliding and
	# the bar changes color as well as length.
	var utilization := float(wheel["utilization"])
	if utilization > 0.02:
		var across := _kart.global_transform.basis.x * utilization * UTILIZATION_SCALE
		var bar_color := COLOR_SLIDING if utilization > 1.0 else COLOR_CONTACT
		var at := point + normal * 0.02
		_line(at - across, at + across, bar_color)


func _line(from: Vector3, to: Vector3, color: Color) -> void:
	_mesh.surface_set_color(color)
	_mesh.surface_add_vertex(from)
	_mesh.surface_set_color(color)
	_mesh.surface_add_vertex(to)


func _cross(at: Vector3, size: float, color: Color) -> void:
	_line(at - Vector3.RIGHT * size, at + Vector3.RIGHT * size, color)
	_line(at - Vector3.UP * size, at + Vector3.UP * size, color)
	_line(at - Vector3.BACK * size, at + Vector3.BACK * size, color)
