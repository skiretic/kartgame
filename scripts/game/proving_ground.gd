extends Node3D

## ROADMAP M3a's proving ground: the first scene in this project you can actually
## drive in.
##
##     godot --path . scenes/game/proving_ground.tscn
##     tools/shots/shoot.sh --scene=res://scenes/game/proving_ground.tscn --out=shots/pg.png
##
## Arguments (all optional, after a bare `--`), plus everything `LookEnv` takes:
##
##   --skidpad=15        constant-radius skidpad marker radius, meters
##   --markers=true      distance markers up the acceleration straight
##   --camera=chase      which rig starts current: chase or free
##   --eye=x,y,z         park a camera here instead, and leave it there
##   --look=x,y,z        what the parked camera aims at (default: the spawn)
##   --hud=true          the telemetry overlay
##   --throttle=1 --steer=0.3
##                       drive the kart from arguments instead of from the input
##                       map, so a still can be taken of a kart that is moving
##
## ## Why a flat plane and not a track
##
## M5 generates the real circuit from `track.json`. What M3a needs is the
## opposite of a circuit: a surface with no variables in it, so that when the kart
## does something surprising the surface is not a suspect. The two markings on it
## are the two §6.4 validation scenarios drawn at true size — a 15 m constant-radius
## skidpad and a 200 m straight ticked every 25 m — so that "0-100 in 3.5 s" and
## "2.0-2.5 g lateral" can be *seen* before M3b's instrumented versions measure
## them properly.
##
## ## What this scene is allowed to own
##
## Not the lighting. `LookEnv` owns that, for the reason its own header gives at
## length: four ambient double-counting bugs so far, every one of them a scene
## that built its own. This scene positions things and nothing else.

const SKIDPAD_RADIUS := 15.0
const STRAIGHT_LENGTH := 200.0
const MARKER_SPACING := 25.0

## Ground extent, meters. Sized from the acceleration run rather than chosen: a
## kart at full throttle covers about 1,200 m before it stops accelerating, and
## at 500 m the §6.4 top-speed scenario drove off the edge and reported terminal
## velocity in free fall as its top speed. Twice the distance the run needs, so
## that overshooting is not a special case.
const GROUND_SIZE := 2400.0

## Ground collider thickness. A thin slab under a fast body invites tunnelling;
## Jolt's continuous detection covers the kart, but the cheaper fix is a collider
## that is 2 m thick and cannot be passed through in one tick at any speed.
const GROUND_THICKNESS := 2.0

## Where the kart starts: at the near end of the straight, facing down it (-Z).
## Well back from the plane's edge for the same reason the plane is large.
const SPAWN := Vector3(0.0, 0.12, 1000.0)

var _args: Dictionary = {}
var _kart: KartDebugVehicle
var _chase: ChaseCamera
var _free: FreeCamera
var _fixed: Camera3D
var _hud: Label
var _physics_draw: PhysicsDraw
var _camera_mode := "chase"


func _ready() -> void:
	_args = Cmdline.parse()

	_build_environment()
	_build_ground()
	_build_markings()
	_build_kart()
	_build_cameras()
	_build_hud()

	_set_camera_mode(Cmdline.as_string(_args, "camera", "chase"))


# --- construction ----------------------------------------------------------


func _build_environment() -> void:
	var world_environment := WorldEnvironment.new()
	world_environment.environment = LookEnv.environment(_args)
	world_environment.name = "WorldEnvironment"
	add_child(world_environment)
	add_child(LookEnv.sun(_args))
	# Sized to the driving area rather than to the whole ground plane: a probe box
	# covering 500 m resolves nothing at kart scale, which is the same mistake
	# `kartview.gd` records having made with the look-dev scene's default box.
	add_child(LookEnv.reflection_probe(
		Vector3(120.0, 20.0, 260.0), Vector3(0.0, 6.0, SPAWN.z - 60.0), 400.0))


func _build_ground() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(GROUND_SIZE, GROUND_SIZE)
	# Subdivided so the vertex-lit fog and the eventual lightmap have somewhere to
	# vary. Free at this triangle count.
	plane.subdivide_width = 32
	plane.subdivide_depth = 32

	var visual := MeshInstance3D.new()
	visual.mesh = plane
	visual.material_override = LookEnv.asphalt_material(GROUND_SIZE)
	visual.name = "GroundVisual"
	add_child(visual)

	var body := StaticBody3D.new()
	body.name = "Ground"
	# Friction is the surface's, not the tire's. ARCHITECTURE.md §6 gives the tire
	# model the grip curve and the surface a multiplier; at M3a there is no tire
	# model, so this stays at 1.0 and `VehicleWheel3D.wheel_friction_slip` carries
	# the whole thing — which is exactly the coarseness M3b exists to remove.
	var physics_material := PhysicsMaterial.new()
	physics_material.friction = 1.0
	physics_material.rough = true
	body.physics_material_override = physics_material

	var shape := BoxShape3D.new()
	shape.size = Vector3(GROUND_SIZE, GROUND_THICKNESS, GROUND_SIZE)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	# Sunk so its top face is exactly y = 0, where the visual plane is. A
	# half-thickness error here reads as a kart floating or half-buried, and it is
	# the first thing to check if it does.
	collider.position = Vector3(0.0, -GROUND_THICKNESS * 0.5, 0.0)
	body.add_child(collider)
	add_child(body)


## The two §6.4 validation scenarios, drawn at true size.
##
## Drawn rather than instrumented: M3b builds the measured versions. These are
## here so that a human driving the kart can see whether it is roughly in the
## right country — a kart that reaches the 100 m marker in under 3 s, or that
## cannot hold the 15 m circle at all, is telling you something before any
## telemetry exists.
func _build_markings() -> void:
	var paint := StandardMaterial3D.new()
	paint.albedo_color = Color(0.86, 0.86, 0.84)
	paint.roughness = 0.85
	# Paint sits on asphalt, not in it. Without this the marker z-fights the
	# ground across the whole plane, which looks like a rendering bug and is one.
	paint.render_priority = 1

	if Cmdline.as_bool(_args, "markers", true):
		# The start line, across the straight at the spawn point, so an
		# acceleration run has a datum rather than starting wherever the kart
		# happened to stop rolling.
		_paint_bar("StartLine", Vector3(0.0, 0.005, SPAWN.z), Vector3(9.0, 0.01, 0.25), paint)

		var count := int(STRAIGHT_LENGTH / MARKER_SPACING)
		for index in range(1, count + 1):
			var distance := float(index) * MARKER_SPACING
			# Both sides of the straight, so the marker is visible whichever way
			# the kart has drifted. 3.5 m out is clear of the racing surface.
			# Names carry the side as well as the distance: two nodes with the
			# same name get a numeric suffix from Godot, and a generated name with
			# a counter in it is the one thing determinism cannot have — the same
			# rule `build.py` holds for Blender object names.
			for side: float in [-3.5, 3.5]:
				var label := "L" if side < 0.0 else "R"
				_paint_bar(
					"Marker%dm%s" % [int(distance), label],
					Vector3(side, 0.005, SPAWN.z - distance),
					Vector3(0.3, 0.01, 2.5),
					paint
				)
			# Every hundred metres gets a line all the way across, because that is
			# the distance the §6.4 acceleration figures are quoted over and a pair
			# of edge markers is easy to pass without noticing.
			if int(distance) % 100 == 0:
				_paint_bar(
					"Line%dm" % int(distance),
					Vector3(0.0, 0.005, SPAWN.z - distance),
					Vector3(9.0, 0.01, 0.25),
					paint
				)

	var radius := Cmdline.as_float(_args, "skidpad", SKIDPAD_RADIUS)
	if radius <= 0.0:
		return
	# The circle is 48 dashes rather than a ring mesh: a dashed line reads as a
	# painted circle at ground level, and a solid ring at 15 m radius reads as a
	# wall in the periphery when you are cornering next to it.
	var segments := 48
	var center := Vector3(0.0, 0.0, SPAWN.z - 60.0)
	for index in segments:
		var angle := TAU * float(index) / float(segments)
		var dash := _paint_bar(
			"Skidpad%02d" % index,
			center + Vector3(sin(angle) * radius, 0.005, cos(angle) * radius),
			Vector3(0.2, 0.01, 1.2),
			paint
		)
		# Turned to follow the circle, so the dashes read as a painted line rather
		# than as a ring of loose tiles.
		dash.rotation.y = -angle


func _paint_bar(
	node_name: String, at: Vector3, size: Vector3, material: Material
) -> MeshInstance3D:
	var bar := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = size
	bar.mesh = box
	bar.material_override = material
	bar.position = at
	bar.name = node_name
	add_child(bar)
	return bar


func _build_kart() -> void:
	_kart = KartDebugVehicle.new()
	_kart.name = "Kart"
	add_child(_kart)
	# Spawned a little above the ground and dropped. Placing it at exactly wheel
	# height instead means the first tick starts with the tires already
	# interpenetrating, and Jolt resolves that by pushing them apart — a kart that
	# hops on spawn is this, not a suspension bug.
	_kart.set_spawn(Transform3D(Basis(), SPAWN))

	_physics_draw = PhysicsDraw.new()
	_physics_draw.name = "PhysicsDraw"
	add_child(_physics_draw)
	_physics_draw.set_target(_kart)

	# Scripted input, for stills and for anything that has no keyboard. Held
	# constant for the whole run: a still of a kart at 90 km/h is one command,
	# and the same command produces the same still, which is the rule
	# `tools/shots/shoot.sh` exists to keep.
	if _args.has("throttle") or _args.has("steer") or _args.has("brake"):
		var throttle := Cmdline.as_float(_args, "throttle", 0.0)
		var brake := Cmdline.as_float(_args, "brake", 0.0)
		var steer := Cmdline.as_float(_args, "steer", 0.0)
		_kart.input_driver = func() -> Dictionary:
			return {"throttle": throttle, "brake": brake, "steer": steer}


func _build_cameras() -> void:
	_chase = ChaseCamera.new()
	_chase.name = "ChaseRig"
	add_child(_chase)
	_chase.set_target(_kart)
	_chase.camera.attributes = LookEnv.camera_attributes(_args)

	_free = FreeCamera.new()
	_free.name = "FreeCamera"
	_free.attributes = LookEnv.camera_attributes(_args)
	add_child(_free)

	# `--eye=x,y,z --look=x,y,z` parks a camera and leaves it there. A chase rig
	# frames whatever the kart is doing, which is right for driving and useless
	# for a still: two runs put the camera in two places and the images cannot be
	# compared. This is the same rule `shoot.sh` holds everywhere else — an image
	# is described by the command that made it.
	var eye := Cmdline.as_string(_args, "eye", "")
	if eye == "":
		return
	_fixed = Camera3D.new()
	_fixed.name = "FixedCamera"
	_fixed.fov = Cmdline.as_float(_args, "fov", 55.0)
	_fixed.near = 0.05
	_fixed.far = 900.0
	_fixed.attributes = LookEnv.camera_attributes(_args)
	add_child(_fixed)
	var target := _parse_point(Cmdline.as_string(_args, "look", ""), SPAWN)
	_fixed.look_at_from_position(_parse_point(eye, Vector3(8.0, 3.0, 26.0)), target, Vector3.UP)


func _parse_point(text: String, fallback: Vector3) -> Vector3:
	if text == "":
		return fallback
	var parts := text.split(",")
	if parts.size() != 3:
		push_warning("expected three comma-separated numbers, got: " + text)
		return fallback
	return Vector3(float(parts[0]), float(parts[1]), float(parts[2]))


func _build_hud() -> void:
	if not Cmdline.as_bool(_args, "hud", true):
		return
	var layer := CanvasLayer.new()
	layer.name = "HUD"
	add_child(layer)

	_hud = Label.new()
	_hud.add_theme_font_size_override("font_size", 15)
	_hud.add_theme_color_override("font_color", Color.WHITE)
	_hud.add_theme_color_override("font_outline_color", Color.BLACK)
	_hud.add_theme_constant_override("outline_size", 5)
	_hud.position = Vector2(14, 12)
	layer.add_child(_hud)


# --- run time --------------------------------------------------------------


func _process(_delta: float) -> void:
	if Input.is_action_just_pressed(&"camera_cycle"):
		_set_camera_mode("free" if _camera_mode == "chase" else "chase")
	if _hud != null:
		_hud.text = _telemetry()


func _set_camera_mode(mode: String) -> void:
	# A parked camera outranks the mode argument: if the command asked for a
	# specific viewpoint, cycling away from it would break the still it was asked
	# for. Driving with `--eye` set is deliberate — it is how a fixed-camera pass
	# of a moving kart gets taken.
	if _fixed != null:
		_camera_mode = "fixed"
		_fixed.current = true
		return
	_camera_mode = "free" if mode == "free" else "chase"
	if _camera_mode == "free":
		_free.take_over(_chase.camera)
		# The kart keeps simulating but stops reading input, because the flight
		# camera reuses the drive keys. Freezing the body instead would make the
		# free camera a different experiment from the one being debugged.
		_kart.set_process_input_enabled(false)
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	else:
		_chase.camera.current = true
		_kart.set_process_input_enabled(true)
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _telemetry() -> String:
	var lines: Array[String] = []
	lines.append("%5.1f km/h   %d/4 wheels down   camera: %s" % [
		_kart.speed_ms * 3.6, _kart.wheels_on_ground, _camera_mode,
	])
	lines.append("throttle %.2f  brake %.2f  steer %+.2f (%+.1f deg)" % [
		_kart.throttle_input, _kart.brake_input, _kart.steer_input,
		rad_to_deg(_kart.steer_input * KartDebugVehicle.STEER_LOCK),
	])
	lines.append("physics %d Hz   frame %.1f fps   %s" % [
		Engine.physics_ticks_per_second,
		Engine.get_frames_per_second(),
		ProjectSettings.get_setting("physics/3d/physics_engine", "?"),
	])
	lines.append("W/S throttle-brake  A/D steer  R respawn  V camera  F11 physics draw")
	return "\n".join(lines)
