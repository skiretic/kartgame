extends Node3D

## ROADMAP M3's proving ground: the first scene in this project you can actually
## drive in.
##
##     godot --path . scenes/game/proving_ground.tscn
##     tools/shots/shoot.sh --scene=res://scenes/game/proving_ground.tscn --out=shots/pg.png
##
## Arguments (all optional, after a bare `--`), plus everything `LookEnv` takes:
##
##   --skidpad=15        constant-radius skidpad marker radius, meters
##   --markers=true      distance markers up the acceleration straight
##   --grass=true        the grass run-off patch beside the straight
##   --camera=chase      which rig starts current: chase or free
##   --eye=x,y,z         park a camera here instead, and leave it there
##   --look=x,y,z        what the parked camera aims at (default: the spawn)
##   --hud=true          the corner text overlay (the F3 telemetry panel is
##                       always added and starts hidden)
##   --throttle=1 --steer=0.3 --brake=0
##                       drive the kart from arguments instead of from the input
##                       map, so a still can be taken of a kart that is moving
##
## ## Why a flat plane and not a track
##
## M5 generates the real circuit from `track.json`. What M3 needs is the opposite
## of a circuit: a surface with no variables in it, so that when the kart does
## something surprising the surface is not a suspect. The markings on it are the
## §6.4 validation scenarios drawn at true size — a 15 m constant-radius skidpad
## and a 200 m straight ticked every 25 m — so that "0-100 in 3.5 s" and the
## cornering figure can be *seen* before the instrumented versions measure them.
##
## ## What this scene owns that the kart does not
##
## The kart's **mesh and its collision shape**. `src/vehicle/kart_body.h` says why
## in its own words: those are asset plumbing rather than physics, and a C++ node
## that loads a gitignored `.glb` cannot be instantiated in a test. So `KartBody`
## carries the mass properties, the rays and the solver, and everything you can
## see or bump into is built here — the same way the ground and the markings are.
##
## Not the lighting, though. `LookEnv` owns that, for the reason its own header
## gives at length: four ambient double-counting bugs so far, every one of them a
## scene that built its own.

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

## The grass run-off, issue #42's acceptance surface.
##
## Placed **beside** the straight rather than on it, and clear of the skidpad, so
## that it is opt-in: no §6.4 scenario drives onto it and none of their numbers
## move. The straight runs down -Z from the spawn with markers at x = +/-3.5 m, so
## a patch starting 10 m out is seven kart widths clear of anything anyone is
## measuring, and the skidpad circle ends at z = SPAWN.z - 45 while this ends at
## z = SPAWN.z - 35.
const GRASS_CENTER := Vector3(20.0, 0.0, SPAWN.z - 20.0)
const GRASS_SIZE := Vector2(20.0, 30.0)

## How far the grass slab's top face stands above the asphalt, meters.
##
## It cannot be zero. Two coplanar collider faces make the suspension raycast's
## answer arbitrary — a wheel would flicker between asphalt and grass along the
## whole boundary — so the grass has to win by being higher, and the only
## question is by how much. 2 mm is the smallest lip that is unambiguous at
## single precision over a 1 km-long ground plane, and it costs one tick of extra
## spring load on the way on: 2 mm against `chassis_flex.h`'s 277,500 N/m rear
## tire rate is 555 N, which the suspension absorbs as the sort of transient a
## painted edge really does produce. Any larger and driving onto grass would
## begin with a jump.
const GRASS_LIP := 0.002

## `kart::core::SurfaceType` from `src/core/surface.h`, which `KartBody`'s ground
## query reads off the collider's `surface_type` metadata. **These integers are a
## wire format** — that header says so — so they are written out here rather than
## derived, and absent metadata means asphalt because asphalt is zero.
const SURFACE_ASPHALT := 0
const SURFACE_GRASS := 2

var _args: Dictionary = {}
## The kart, its driver, its wheel pivots and its three voices. One object, so this
## scene and `test_track.gd` and `circuit.gd` build the same kart rather than three
## karts that drifted apart — which is what they were. Issue #183.
var _rig := KartRig.new()
var _kart: KartBody
## Where this run's assist state came from — a stored `settings.cfg`, a command
## line, or a scripted run that is not allowed to read one.
## `scripts/game/assist_settings.gd` resolves it and this holds the report.
var _assists: Dictionary = {}
## The human at the controls. ADR-0040: `KartBody` no longer reads the `Input`
## singleton, so without this node the kart coasts and the pad does nothing.
var _driver: PlayerDriver
var _chase: ChaseCamera
var _free: FreeCamera
var _cockpit: CockpitCamera
var _fixed: Camera3D
var _hud: Label
var _driving_hud: Control

var _physics_draw: PhysicsDraw
var _camera_mode := "chase"

## Ticks to leave the kart's `_physics_process` switched off after a respawn.
## See `_respawn()` — this is issue #132 and one tick is the whole of it.
const RESPAWN_QUIET_TICKS := 1
var _respawn_quiet := 0


func _ready() -> void:
	_args = Cmdline.parse()

	_build_environment()
	_build_ground()
	_build_markings()
	_build_grass()
	_build_kart()
	_build_cameras()
	_build_hud()
	# One line, and it is the whole of issue #43's integration: `KartBody::_ready`
	# joins the `telemetry_source` group and the panel finds it there. Nothing
	# here holds a reference to either, which is what stops the two being wired
	# together by hand and drifting.
	add_child(preload("res://scenes/ui/telemetry.tscn").instantiate())

	_set_camera_mode(Cmdline.as_string(_args, "camera", "chase"))

	# The only thing this scene prints, and it earns the line: every §6.4 figure is
	# measured here, so a run has to be able to say whether the assists it drove
	# with came from a file nobody mentioned. A stored preference silently moving a
	# validation number is the failure this whole path was built to avoid.
	print("proving ground: %s" % AssistSettings.describe(_assists).strip_edges())


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
	# Friction is the *tire model's*, not this material's. `KartBody::_ready` sets
	# its own body's `physics_material_override.friction` to 0.0 and the combine
	# rule is measured to be `min(a, b)` (ADR-0033), so whatever is written here
	# reaches nothing — it is left at 1.0 so that anything else which ever lands
	# on this plane behaves normally. The number that does matter is the surface
	# type below, which `src/core/surface.h` turns into a grip multiplier.
	var physics_material := PhysicsMaterial.new()
	physics_material.friction = 1.0
	physics_material.rough = true
	body.physics_material_override = physics_material
	# The key `KartBody::query_ground` reads off whatever a suspension ray hit.
	# Stated rather than left to the default: asphalt is zero and absent metadata
	# already means asphalt, but a track surface that says what it is out loud is
	# the thing a new surface gets added next to.
	body.set_meta("surface_type", SURFACE_ASPHALT)

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


## The grass run-off, and issue #42's acceptance: "driving onto grass loses grip
## immediately and obviously."
##
## It is one collider carrying one piece of metadata. `KartBody::query_ground`
## reads `surface_type` off whatever each suspension ray hit, `src/core/surface.h`
## turns that into a multiplier — 0.18 for grass, against asphalt's 1.00 — and
## `TireSlip::surface_grip` scales both tire axes by it. Nothing in this file
## knows any of that; it says "grass" and the solver does the rest, which is the
## test of whether #42's hook is a hook or a special case.
##
## 0.18 is not a road-car ratio. `surface.h` is emphatic about it: the shear plane
## on grass is inside the turf, so the published coefficient transfers as an
## absolute and is divided by the slick's own 2.10 rather than scaled off it. In
## round numbers a kart that holds 1.9 g on asphalt holds 0.34 g here, so the
## front washes out at walking pace and the rear steps out under any throttle at
## all. That is meant to be alarming.
func _build_grass() -> void:
	if not Cmdline.as_bool(_args, "grass", true):
		return

	var material := StandardMaterial3D.new()
	material.albedo_texture = _grass_texture()
	# Rough and unmetallic, and no normal map: this is a marked-out surface for a
	# physics acceptance test, not a landscape. The look budget belongs to the
	# kart. What it does have to be is unmistakably *not asphalt* at a glance from
	# a chase camera, which is what the hue and the tiling do.
	material.roughness = 0.95
	material.metallic = 0.0
	material.uv1_scale = Vector3(GRASS_SIZE.x * 0.5, GRASS_SIZE.y * 0.5, 1.0)

	# The slab is 0.5 m thick and mostly buried. Only the top face stands proud of
	# the asphalt, by `GRASS_LIP`, so nothing coplanar is ever drawn or cast
	# against — the sides are vertical and cannot z-fight with a horizontal plane.
	var thickness := 0.5
	var center := Vector3(
		GRASS_CENTER.x, GRASS_LIP - thickness * 0.5, GRASS_CENTER.z
	)

	var visual := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(GRASS_SIZE.x, thickness, GRASS_SIZE.y)
	visual.mesh = box
	visual.material_override = material
	visual.position = center
	visual.name = "GrassVisual"
	add_child(visual)

	var body := StaticBody3D.new()
	body.name = "Grass"
	# The one line that makes this grass rather than green asphalt.
	body.set_meta("surface_type", SURFACE_GRASS)
	var shape := BoxShape3D.new()
	shape.size = box.size
	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.name = "GrassShape"
	body.position = center
	body.add_child(collider)
	add_child(body)

	# A painted edge on the two sides a kart can arrive from, so leaving the
	# asphalt is a decision rather than a surprise. Real circuits mark this line
	# and this scene is not the place to be subtle about a grip cliff.
	var edge := StandardMaterial3D.new()
	edge.albedo_color = Color(0.86, 0.86, 0.84)
	edge.roughness = 0.85
	edge.render_priority = 1
	_paint_bar(
		"GrassEdgeInner",
		Vector3(GRASS_CENTER.x - GRASS_SIZE.x * 0.5, 0.006, GRASS_CENTER.z),
		Vector3(0.15, 0.01, GRASS_SIZE.y),
		edge
	)
	_paint_bar(
		"GrassEdgeNear",
		Vector3(GRASS_CENTER.x, 0.006, GRASS_CENTER.z + GRASS_SIZE.y * 0.5),
		Vector3(GRASS_SIZE.x, 0.01, 0.15),
		edge
	)


## A 2 m grass tile, generated rather than loaded.
##
## `LookEnv` owns the photoscanned surfaces and there is no grass scan in the
## project; fetching one is `docs/REFERENCES.md`'s business and a reference rule
## applies to it. What this needs meanwhile is something that reads as turf from
## ten meters and is the same every run, so it is two greens of noise at a fixed
## seed — the same trick, and the same determinism rule, as
## `LookEnv.checker_material`.
func _grass_texture() -> ImageTexture:
	const TEXTURE_SIZE := 128
	var image := Image.create(TEXTURE_SIZE, TEXTURE_SIZE, false, Image.FORMAT_RGB8)
	var noise := RandomNumberGenerator.new()
	noise.seed = 0x67726173  # "gras" — fixed, so the texture is the same every run
	for y in TEXTURE_SIZE:
		for x in TEXTURE_SIZE:
			# Two scales of variation: a coarse one that reads as mown stripes and
			# clumps at distance, a fine one that stops the surface looking like
			# flat paint up close.
			var clump := sin(float(x) * 0.19) * 0.5 + sin(float(y) * 0.11) * 0.5
			var value := 0.5 + clump * 0.16 + noise.randf_range(-0.20, 0.20)
			image.set_pixel(x, y, Color(
				clampf(0.055 + value * 0.075, 0.0, 1.0),
				clampf(0.130 + value * 0.190, 0.0, 1.0),
				clampf(0.030 + value * 0.055, 0.0, 1.0)
			))
	return ImageTexture.create_from_image(image)


## The kart: a `KartBody`, plus the two things it deliberately does not own.
##
## `src/vehicle/kart_body.h` puts the mesh and the collision shape here on
## purpose — they are asset plumbing rather than physics, and a C++ node that
## loads a gitignored `.glb` cannot be instantiated in a test. Both moved here
## verbatim from the deleted `kart_debug_vehicle.gd` and are unchanged in
## behavior; what left with that file is every physics constant it carried,
## because those all live in `src/core/` now where a test with no engine running
## can reach them.
##
## The node is named "Kart" and that name is load-bearing:
## `tools/verify/drive_probe.gd` finds it with `find_child("Kart", false, false)`.
func _build_kart() -> void:
	# The mesh, the chassis box, the `PlayerDriver` beside the body, the three voices
	# and the listener — `KartRig` owns all of it and this scene owns where the road
	# is. `SPAWN` already carries the ride height, which is `KartRig.SPAWN_LIFT` and
	# the same 0.12 m for the same reason: placing the kart at exactly wheel height
	# starts the first tick with the tires already interpenetrating and Jolt resolves
	# that by pushing them apart, so a kart that hops on spawn is this and not a
	# suspension bug.
	#
	# The scripted-input branch is in there too, and it stays ahead of nothing that
	# reads it — `AssistSettings.apply` below decides on `_args`, not on whether
	# `input_driver` has been set, so the order of these two is not load-bearing.
	_kart = _rig.build(self, Transform3D(Basis(), SPAWN), _args)
	_driver = _rig.driver

	# The assists: whatever the driver last chose, then whatever this command says.
	# `--auto-shift=off` starts in the manual box, so a session that is about the
	# gearbox does not begin by reaching for a key.
	#
	# **This scene is the one that made the rule** inside `assist_settings.gd`.
	# Every §6.4 figure is measured here and `shoot.sh` drives it from `--throttle`
	# and friends, so a `settings.cfg` in `user://` with auto-shift off would move
	# a validation number and a published still without appearing in either
	# command. The helper skips the stored file whenever the input is scripted, and
	# says which rule it applied rather than deciding quietly.
	_assists = AssistSettings.apply(_kart, _args)

	_physics_draw = PhysicsDraw.new()
	_physics_draw.name = "PhysicsDraw"
	add_child(_physics_draw)
	_physics_draw.set_target(_kart)


func _build_cameras() -> void:
	_chase = ChaseCamera.new()
	_chase.name = "ChaseRig"
	add_child(_chase)
	_chase.set_target(_kart)
	_chase.camera.attributes = LookEnv.camera_attributes(_args)

	# The cockpit rig, parented to the kart rather than to the scene: the eye is a
	# position in the body frame and a rig in the scene would have to re-derive the
	# kart's transform every frame to put it there.
	_cockpit = CockpitCamera.new()
	_cockpit.name = "CockpitRig"
	_kart.add_child(_cockpit)
	_cockpit.set_target(_kart)
	_cockpit.camera.attributes = LookEnv.camera_attributes(_args)

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
	# Scaled to the viewport, not fixed. This was a hard 15 px and the first real
	# drive reported it as unreadable — at Godot's old 1152x648 default it was 15 px
	# in a small window, and with `canvas_items` stretch now on it would have stayed
	# 15 px of a 1600x900 canvas however large the window got. 20 px at 1080 with a
	# floor of 15 so it cannot shrink below where it started.
	# `driving_hud.gd` uses the same `viewport.y / 1080` reference, deliberately, so
	# the two overlays grow together.
	_hud.add_theme_font_size_override("font_size",
			maxi(15, int(round(20.0 * get_viewport().get_visible_rect().size.y / 1080.0))))
	_hud.add_theme_color_override("font_color", Color.WHITE)
	_hud.add_theme_color_override("font_outline_color", Color.BLACK)
	_hud.add_theme_constant_override("outline_size", 5)
	_hud.position = Vector2(14, 12)
	layer.add_child(_hud)

	# The driving HUD, on the same layer and under the same flag. Two overlays and
	# not one, because they answer different questions: the Label above is a
	# developer read-out with the key bindings on it, and `driving_hud.gd` is the
	# instrument a driver judges the kart with — issue #73, pulled forward from M6
	# by #138. Its own header argues why those cannot be the same widget, in the
	# same terms that keep the F3 telemetry panel separate from both.
	#
	# `bind()` rather than an exported property: the HUD reads the limiter and
	# rollover thresholds off the body once, and `_build_kart()` has already run by
	# the time this does, so there is a body to read them from.
	_driving_hud = preload("res://scripts/ui/driving_hud.gd").new()
	_driving_hud.name = "DrivingHud"
	layer.add_child(_driving_hud)
	if _kart != null:
		_driving_hud.bind(_kart)


# --- run time --------------------------------------------------------------


func _process(_delta: float) -> void:
	if Input.is_action_just_pressed(&"camera_cycle"):
		_set_camera_mode(_next_camera_mode())
	# The gearbox. `drivetrain.h` defaults `auto_shift` on and until now **nothing
	# could turn it off** — no key, no pad button, no command-line flag, no registry
	# row — so R1 and L1 fired `request_shift_up` into an assist that immediately
	# shifted again on its own, and the box read as automatic. Issue #40's "assists
	# off" was never reachable from the game.
	#
	# Auto-shift only, and auto-clutch deliberately left alone: `project.godot`
	# binds the clutch to a **button** rather than an axis, because a DualSense's
	# two triggers are already throttle and brake. An unassisted launch needs clutch
	# modulation (#38) and a digital clutch cannot supply it, so "assists off" on a
	# pad would be a mode nobody can drive away in.
	#
	# Remembered on the way out, so the next launch starts where the driver left
	# it. There is no session in this scene to keep in step — `test_track.gd` has
	# that half of the fix and the reason it needed one.
	if _kart != null and Input.is_action_just_pressed(&"auto_shift_toggle"):
		_kart.auto_shift = not _kart.auto_shift
		AssistSettings.remember(_kart)
	# `look_back` has been bound in `project.godot` and printed in this HUD since
	# M3a, and until now **nothing read it** — C and Triangle did nothing at all.
	# That is the exact failure CLAUDE.md's driving section opens with, one level
	# worse: the key was advertised rather than omitted. The chase rig's own
	# look-back is separate work and has its own issue; this is the cockpit's, where
	# looking back is a head turn and is three lines.
	if _cockpit != null:
		_cockpit.set_looking_back(Input.is_action_pressed(&"look_back"))
	# Respawn is the scene's, not the kart's. `KartBody` reads throttle, brake,
	# steer, clutch and the two shift edges off the input map, and deliberately
	# stops there: `respawn()` moves a body to a pose only whoever placed it knows,
	# so it is a method someone calls rather than an action a node listens for. The
	# deleted GDScript vehicle read the action itself, so without this line R on
	# the keyboard and Circle on the pad do nothing at all and the first time the
	# kart ends up on its roof the session is over.
	if _kart != null and Input.is_action_just_pressed(&"respawn"):
		_respawn()
	if _hud != null:
		_hud.text = _hud_text()


func _physics_process(_delta: float) -> void:
	# Issue #132's quiet tick, ticked down here so it is counted in physics steps
	# rather than in rendered frames. See `_respawn()`.
	if _respawn_quiet > 0:
		_respawn_quiet -= 1
		if _respawn_quiet == 0:
			_kart.set_physics_process(true)
	# The rule this keeps is the one the deleted vehicle stated: a wheel that is not
	# turning on screen is a wheel that is not turning in the sim. `KartRig` owns the
	# how, and its own header records that the extraction read the wrong key for a
	# milestone and drew a straight wheel through every corner on `valdirone.tscn`.
	_rig.update_wheel_visuals()


## Back to the grid, with one force-free tick after it.
##
## Issue #132: applying a force at an offset on the same tick a body is teleported
## resolves the lever arm against a center-of-mass position the teleport has not
## reached yet, and the body picks up about 2.1e-3 rad/s of spin that nothing
## asked for. `tools/verify/kart_body_probe.gd` measured it — identical forces,
## different torque, a velocity difference of exactly zero beside a spin
## difference that is not — and skips one tick for the same reason. Here it is the
## difference between a kart that respawns upright and one that respawns already
## rolling slightly.
func _respawn() -> void:
	_kart.respawn()
	_kart.set_physics_process(false)
	_respawn_quiet = RESPAWN_QUIET_TICKS + 1
	# The HUD's peak and sustained figures are session records, and a session ends
	# at the grid. Without this a 2.4 g spike from the spin that caused the respawn
	# outlives the spin and sits on the panel for the rest of the evening, which
	# makes the one number every M3b acceptance criterion reduces to a lie about a
	# lap that is over.
	if _driving_hud != null:
		_driving_hud.reset_peaks()


func _set_camera_mode(mode: String) -> void:
	# A parked camera outranks the mode argument: if the command asked for a
	# specific viewpoint, cycling away from it would break the still it was asked
	# for. Driving with `--eye` set is deliberate — it is how a fixed-camera pass
	# of a moving kart gets taken.
	if _fixed != null:
		_camera_mode = "fixed"
		_fixed.current = true
		return
	if mode == "cockpit" and _cockpit == null:
		mode = "chase"
	# Captured before the assignment, because the free camera takes over from
	# wherever the view currently *is*. Handing it the chase camera while the
	# driver is in the cockpit teleports the view three meters backwards on a key
	# press, which reads as the free camera being broken.
	var previous := _camera_mode
	_camera_mode = mode if mode in ["free", "cockpit"] else "chase"
	if _camera_mode == "free":
		_free.take_over(_cockpit.camera if previous == "cockpit" else _chase.camera)
		# The kart keeps simulating but the driver stops reading the pad, because the
		# flight camera reuses the drive keys. Freezing the body instead would make
		# the free camera a different experiment from the one being debugged.
		#
		# The driver keeps *pushing* — neutral — rather than going quiet, so
		# `KartBody`'s freshness check stays satisfied. See `player_driver.h`.
		_driver.enabled = false
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
		return
	if _camera_mode == "cockpit":
		_cockpit.make_current()
	else:
		_chase.camera.current = true
	_driver.enabled = true
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


## Chase, cockpit, free, round again.
##
## Cockpit second rather than last because it is the one somebody switches to in
## order to drive, and free is a debugging tool that happens to share the key.
func _next_camera_mode() -> String:
	match _camera_mode:
		"chase":
			return "cockpit"
		"cockpit":
			return "free"
		_:
			return "chase"


## The four-line corner overlay.
##
## Deliberately not the telemetry panel: this is what you glance at while driving,
## and F3's panel is what you read when you have stopped. Everything here is a
## property or a getter on `KartBody`, so a rename in the boundary shows up as a
## missing property at run time rather than as a number that has quietly stopped
## moving.
func _hud_text() -> String:
	var lines: Array[String] = []
	# The gearbox mode is on the glance line rather than the telemetry panel because
	# it changes what the driver's own hands do. Without it, "the box is automatic"
	# and "I have auto-shift on" are the same observation and only one of them is a
	# bug report.
	lines.append("%5.1f km/h   gear %d%s   %5.0f rpm   %d/4 wheels down   camera: %s" % [
		_kart.speed_ms * 3.6, _kart.get_gear(),
		" auto" if _kart.auto_shift else " MANUAL",
		_kart.get_engine_rpm(), _kart.wheels_on_ground, _camera_mode,
	])
	lines.append("throttle %.2f  brake %.2f  steer %+.2f (%+.1f deg)   %+.2f lat  %+.2f long g" % [
		_kart.throttle_input, _kart.brake_input, _kart.steer_input,
		# `get_steer_lock()` is `steering.h`'s figure, served by the boundary. The
		# HUD used to read a GDScript constant that had to be kept in step with the
		# solver's by hand, which is one copy of a single-owner number too many.
		rad_to_deg(_kart.steer_input * _kart.get_steer_lock()),
		_kart.get_lateral_g(), _kart.get_longitudinal_g(),
	])
	lines.append("physics %d Hz   frame %.1f fps   %s" % [
		Engine.physics_ticks_per_second,
		Engine.get_frames_per_second(),
		ProjectSettings.get_setting("physics/3d/physics_engine", "?"),
	])
	# The keys, as the InputMap actually resolves them. They were listed as F11 and
	# F12 here for a milestone and neither key is bound to anything — a driver who
	# presses F11, sees nothing and concludes the physics draw is broken is a
	# plausible half-hour that no headless gate can catch. Read off project.godot's
	# `debug_*` actions: F3, F4, F5.
	lines.append_array(ControlHints.lines())
	return "\n".join(lines)
