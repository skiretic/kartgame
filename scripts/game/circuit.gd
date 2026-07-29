extends Node3D

## A circuit, built entirely from `track.json`. ROADMAP M5, ADR-0046, issue #63.
##
##     godot --path . scenes/game/valdirone.tscn
##     godot --path . scenes/game/valdirone.tscn -- --layout=reverse
##
## Arguments (all optional, after a bare `--`), plus everything `LookEnv` takes:
##
##   --track=res://data/tracks/valdirone_nuova.track.json
##                       which circuit to load
##   --layout=forward    forward or reverse — the **authored** reverse layout,
##                       not a spline direction flag
##   --grid=1            which grid slot to start in, 1..8
##   --camera=chase      which rig starts current: chase, cockpit or free
##   --eye=x,y,z         park a camera here instead, and leave it there
##   --look=x,y,z        what the parked camera aims at (default: the grid)
##   --hud=true          the driving HUD and the corner overlay
##   --curbs=true        build the kerb colliders
##   --runoff=true       build the run-off aprons, gravel and barriers
##   --mesh=true         load the generated `.glb` for the visual track; false
##                       draws the collider's own triangles instead, which is what
##                       to use when the question is whether the two agree
##   --session=practice  practice, qualifying, heat, super_heat or final
##   --laps=3            override the session's scheduled limit
##   --steer-gamma=3.0   the steering input curve, through the tuning registry
##   --tune=key=value[,key=value]
##   --preset=path       load a saved preset before the first tick
##   --throttle=1 --steer=0.3 --brake=0
##                       drive from arguments instead of from the input map
##
## ## What this scene is, against the two that already exist
##
## `proving_ground.tscn` is a flat plane where every §6.4 figure is measured, and
## `test_track.tscn` is a 1,030 m diagnostic instrument built **in code** to
## provoke four named M3b behaviours. Neither is a racing circuit and neither
## reads a file.
##
## This one is the file. Every number in it — the 15 m hairpin, the 4.60% descent,
## the 9 m width at Il Ciglione, the kerb that is struck at 129 km/h — comes out of
## `data/tracks/valdirone_nuova.track.json`, which came out of
## `docs/circuits/valdirone_nuova.json`, which came out of a design exercise whose
## sourced checks are in `docs/circuits/README.md`. Nothing here chooses a
## dimension.
##
## ## The session, and which numbers it takes from the file
##
## Issue #180. A `SessionRunner` is built here and it is handed the `KartTrack`
## itself rather than a `TrackLayout` — `configure()`'s header carries the duck type
## and why the two cannot share a base class. Three things come off the file that a
## code-built track has no way to publish:
##
##   * **The sector marks**, at 524 m and 902 m forward and 473 m and 862 m
##     reversed. They are a diagnostic partition chosen by the design rather than
##     thirds, and the reverse layout has its own because the same three splits
##     driven backwards would land in different corners.
##   * **The fourteen checkpoints**, which are the anti-cut resolution and are
##     crossed for their ordering alone. `lap_timing.h` keeps them distinguishable
##     from the splits all the way down, because a timer that made every mark a
##     sector reported this circuit as having sixteen of them.
##   * **The width, station by station.** `_measure_off_track` used a constant 8 m,
##     which is the test track's single width; here it tapers, so track limits are
##     measured against the asphalt that is actually there.
##
## The **content hash** goes into the session configuration, so a lap time recorded
## here is filed against the exact file that produced the geometry. ADR-0041: a
## replay recorded before a corner was smoothed refuses to play back and says which
## file moved.
##
## ## What it deliberately does not do yet
##
## **No runtime scatter and no lightmap bake.** Both are M5 bullets and neither is
## in this pass; the mesh carries a UV2 channel ready for the bake and
## `tools/bake/preflight.gd` is the gate that would run first. Issue #182.
##
## **No pit lane.** The stations are in the file and the *asphalt* is not:
## reversed, a deceleration lane at 20° to the direction of travel is a 160° merge
## over Part I art 7.4's 30° cap, so each layout needs its own stubs. That is
## geometry rather than furniture and it is its own piece of work. Issue #181.

const DEFAULT_TRACK := "res://data/tracks/valdirone_nuova.track.json"
const TRACK_MESH_PATH := "res://assets/generated/valdirone_nuova.glb"

## `kart::core::SurfaceType` from `src/core/surface.h`. **These integers are a wire
## format** — that header says so — so they are written out rather than derived.
const SURFACE_NAMES: PackedStringArray = ["asphalt", "curb", "grass", "dirt"]

## Run-off slab thickness, meters, and how far it reaches past the circuit.
##
## `test_track.gd`'s argument holds and matters more here: a thin slab under a fast
## body invites tunneling, and this circuit has a kart arriving at a kerb at
## 129 km/h by design. 2 m cannot be passed through in one tick at any speed the
## kart reaches. The margin is not a safety figure — it is so a kart that spins off
## ends up on grass rather than off the edge of the world, and so `--eye` can be
## parked well outside for an overhead still without framing the void.
const GROUND_THICKNESS := 2.0
const RUNOFF_MARGIN := 120.0

## `test_track.gd`'s table, and the same reason it is a table: `--session=` names a
## type and a typo has to name the five rather than pick one.
const SESSION_TYPES := {
	"practice": KartSession.TYPE_PRACTICE,
	"qualifying": KartSession.TYPE_QUALIFYING,
	"heat": KartSession.TYPE_HEAT,
	"super_heat": KartSession.TYPE_SUPER_HEAT,
	"final": KartSession.TYPE_FINAL,
}

var _args := {}
var _track: KartTrack
var _rig := KartRig.new()
var _kart: KartBody
var _driver: PlayerDriver
var _session: KartSession
var _runner: SessionRunner
var _timing_hud: TimingHud
var _tuning: KartTuning
var _tuning_panel: Node
var _chase: ChaseCamera
var _cockpit: CockpitCamera
var _free: FreeCamera
var _fixed: Camera3D
var _physics_draw: PhysicsDraw
var _hud: Label
var _driving_hud: Node
var _camera_mode := "chase"
var _spawn := Transform3D()
var _assists := {}
var _hint := -1.0
var _surface_text := ""
var _triangles := {}


func _ready() -> void:
	_args = Cmdline.parse()

	_track = KartTrack.new()
	var path := Cmdline.as_string(_args, "track", DEFAULT_TRACK)
	var error := _track.load(path)
	if error != OK:
		# Refuses rather than warns, and prints every reason rather than the first.
		# A track that loads and cannot be raced is worse than one that will not
		# load: the failure otherwise surfaces three hundred meters into a session,
		# at a corner nobody can take, as a physics bug.
		push_error("%s refused (%d):\n  %s" % [path, error, "\n  ".join(_track.problems())])
		for problem in _track.problems():
			printerr("  ! ", problem)
		return

	var wanted := Cmdline.as_string(_args, "layout", "forward")
	if not _track.select_layout(wanted):
		push_error("no layout %s in %s; have %s" % [wanted, path, _track.layout_names()])
		_track.select_layout(_track.layout_names()[0])

	_build_environment()
	_build_ground()
	_build_track()
	_build_kart()
	_build_tuning()
	# After the tuning, because `adopt_tuning` records what the session was driven
	# under and the `--tune`/`--preset` arguments have been applied by then; before
	# the HUD, because the timing screen binds to the runner and a HUD built first
	# would bind to null and draw nothing all session.
	_build_session()
	_build_cameras()
	_build_hud()
	add_child(preload("res://scenes/ui/telemetry.tscn").instantiate())

	_set_camera_mode(Cmdline.as_string(_args, "camera", "chase"))
	_report()


# --- construction ----------------------------------------------------------


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


func _world_extent() -> float:
	var box := _bounds()
	return maxf(box.size.x, box.size.z) + RUNOFF_MARGIN * 2.0


func _build_environment() -> void:
	var world_environment := WorldEnvironment.new()
	world_environment.environment = LookEnv.environment(_args)
	world_environment.name = "WorldEnvironment"
	add_child(world_environment)
	add_child(LookEnv.sun(_args))

	# Sized over the **whole world** rather than over the driving area, which
	# `test_track.gd` measured rather than chose: a probe box covering only the
	# layout puts a hard rectangular seam across the grass at its own boundary,
	# because `AMBIENT_DISABLED` stops a probe adding ambient but not specular, and
	# broad rough-surface specular is exactly what a lawn is made of.
	var extent := _world_extent()
	var centre := _bounds().get_center()
	add_child(LookEnv.reflection_probe(
		Vector3(extent, 120.0, extent),
		Vector3(centre.x, centre.y + 20.0, centre.z),
		extent
	))


## The run-off: one slab under everything, `surface_type` 2.
##
## A field with a circuit-shaped hole in it would be the physically honest shape
## and it is a great deal of geometry for nothing — the road stands above this and
## wins every suspension raycast that lands on it.
##
## **Sunk to the circuit's lowest point rather than to y = 0**, which the test
## track does not have to think about because it is flat. Valdirone's hairpin sits
## 10.8 m below the start line; a slab at zero would put the bottom of the bowl
## underground and the kart would drive into a hillside made of grass.
func _build_ground() -> void:
	var box := _bounds()
	var extent := _world_extent()
	var centre := box.get_center()
	var floor_y := box.position.y - 0.5

	var plane := PlaneMesh.new()
	plane.size = Vector2(extent, extent)
	plane.subdivide_width = 64
	plane.subdivide_depth = 64

	var visual := MeshInstance3D.new()
	visual.mesh = plane
	var material := TrackRibbon.grass_material()
	material.uv1_scale = Vector3(extent * 0.5, extent * 0.5, 1.0)
	visual.material_override = material
	visual.position = Vector3(centre.x, floor_y, centre.z)
	visual.name = "GroundVisual"
	add_child(visual)

	var body := StaticBody3D.new()
	body.name = "Ground"
	# Friction is the *tire model's*, not this material's — `KartBody::_ready` sets
	# its own body's friction to 0.0 and the combine rule is measured to be
	# `min(a, b)` (ADR-0033), so whatever is written here reaches nothing. The
	# number that matters is the surface type.
	var physics_material := PhysicsMaterial.new()
	physics_material.friction = 1.0
	physics_material.rough = true
	body.physics_material_override = physics_material
	body.set_meta("surface_type", 2)

	var shape := BoxShape3D.new()
	shape.size = Vector3(extent, GROUND_THICKNESS, extent)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.position = Vector3(centre.x, floor_y - GROUND_THICKNESS * 0.5, centre.z)
	body.add_child(collider)
	add_child(body)


## The circuit: colliders from `KartTrack`, mesh from `gentrack.py`.
##
## **One definition, two consumers** — `ARCHITECTURE.md` §11 — and this is the one
## place both of them are in the same tree at the same time, which is what makes
## `--mesh=false` worth having: it draws the *collider's* triangles instead of the
## generated mesh, so a disagreement between the two becomes visible rather than
## remaining a claim. `tools/verify/circuit.sh` measures the same thing in numbers.
func _build_track() -> void:
	var want_curbs := Cmdline.as_bool(_args, "curbs", true)
	var want_runoff := Cmdline.as_bool(_args, "runoff", true)
	var draw_collider := not Cmdline.as_bool(_args, "mesh", true)

	for entry in _track.surface_meshes():
		var surface_name: String = entry["name"]
		if surface_name == "Kerbs" and not want_curbs:
			continue
		if (surface_name == "Gravel" or surface_name == "Barriers") and not want_runoff:
			continue
		var faces: PackedVector3Array = entry["faces"]
		_triangles[surface_name] = faces.size() / 3
		add_child(_static_body(surface_name, faces, int(entry["surface_type"])))
		if draw_collider:
			add_child(_collider_visual(surface_name, faces, int(entry["surface_type"])))

	if draw_collider:
		return
	if not ResourceLoader.exists(TRACK_MESH_PATH):
		push_warning(
			"no track mesh at %s — run tools/blender/gentrack.sh. " % TRACK_MESH_PATH
			+ "Driving works; there is nothing to look at but the colliders."
		)
		return
	var mesh := (load(TRACK_MESH_PATH) as PackedScene).instantiate() as Node3D
	mesh.name = "TrackMesh"
	add_child(mesh)


## One `StaticBody3D` carrying one piece of surface metadata over one collider.
##
## `test_track.gd`'s, and its reasoning holds exactly: `backface_collision` is on
## because a road built as a *surface* rather than as a volume has a failure mode a
## slab does not — anything that ends up underneath it falls through the world.
func _static_body(node_name: String, faces: PackedVector3Array, surface: int) -> StaticBody3D:
	var body := StaticBody3D.new()
	body.name = node_name
	var physics_material := PhysicsMaterial.new()
	physics_material.friction = 1.0
	physics_material.rough = true
	body.physics_material_override = physics_material
	body.set_meta("surface_type", surface)

	var shape := ConcavePolygonShape3D.new()
	shape.set_faces(faces)
	shape.backface_collision = true
	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.name = node_name + "Shape"
	body.add_child(collider)
	return body


## The collider's own triangles, drawn. Only under `--mesh=false`.
func _collider_visual(node_name: String, faces: PackedVector3Array, surface: int) -> MeshInstance3D:
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = faces
	var built := ArrayMesh.new()
	built.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

	var visual := MeshInstance3D.new()
	visual.name = node_name + "ColliderVisual"
	visual.mesh = built
	var material := StandardMaterial3D.new()
	material.albedo_color = [
		Color(0.20, 0.20, 0.22), Color(0.62, 0.09, 0.09),
		Color(0.13, 0.32, 0.09), Color(0.48, 0.43, 0.36),
	][clampi(surface, 0, 3)]
	material.roughness = 0.9
	# Two-sided, because the collider is two-sided: a one-sided draw of a
	# `backface_collision` shape shows holes where the collider has none, which
	# reads as a gap in the road and is the opposite of what this mode is for.
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	visual.material_override = material
	return visual


func _build_kart() -> void:
	var slot := clampi(Cmdline.as_int(_args, "grid", 1), 1, maxi(1, _track.grid_count())) - 1
	_spawn = _track.grid_transform(slot, KartRig.SPAWN_LIFT)
	_kart = _rig.build(self, _spawn, _args)
	_driver = _rig.driver

	# Whatever the driver last chose, then whatever this command says. The rule that
	# matters is `assist_settings.gd`'s: a stored preference is skipped whenever
	# input is scripted, because a `user://settings.cfg` nobody mentioned must never
	# move a validation number or a published still.
	_assists = AssistSettings.apply(_kart, _args)

	_physics_draw = PhysicsDraw.new()
	_physics_draw.name = "PhysicsDraw"
	add_child(_physics_draw)
	_physics_draw.set_target(_kart)


func _build_tuning() -> void:
	if _kart == null:
		return
	_tuning = KartTuning.new()
	_tuning.name = "Tuning"
	add_child(_tuning)
	# After `add_child`, because the path is resolved relative to this node and the
	# setter re-applies immediately once it is in the tree.
	_tuning.set_vehicle_path(_tuning.get_path_to(_kart))
	# The controller half. `steer_gamma` is `TuningOwner::Controller` and the
	# registry deliberately does not know how to reach a driver node, so the driver
	# subscribes instead. Without this line the overlay's steering row moves a
	# number nothing reads.
	if _driver != null:
		_driver.tuning_path = _driver.get_path_to(_tuning)

	var voice_player := _kart.get_node_or_null("EngineVoice") as AudioStreamPlayer3D
	if voice_player != null:
		EngineVoiceRig.bind_tuning(_tuning, _rig.engine_voice, voice_player,
				_rig.scrub_voice, _rig.wind_voice)

	if _args.has("preset"):
		var preset := Cmdline.as_string(_args, "preset", "")
		if preset != "" and not _tuning.load_preset(preset):
			push_error("could not load preset %s" % preset)
	if _args.has("tune"):
		for pair in Cmdline.as_string(_args, "tune", "").split(",", false):
			var halves := pair.split("=")
			if halves.size() == 2:
				_apply_tuning_argument(halves[0].strip_edges(), float(halves[1]))
	if _args.has("steer-gamma"):
		_apply_tuning_argument("steer_gamma", Cmdline.as_float(_args, "steer-gamma", 3.0))


## `test_track.gd`'s, unchanged. The registry keys tunables by integer id and a
## defended override has to be acknowledged before it will move, so a caller that
## reaches for `set_value(name, value)` gets a parse error and a caller that
## forgets `acknowledge` gets a silently refused change.
func _apply_tuning_argument(key: String, value: float) -> void:
	var id: int = _tuning.id_of(key)
	if id < 0:
		push_error("no tunable named '%s' — run tools/verify/tuning.sh to list them" % key)
		return
	if _tuning.is_defended(id):
		_tuning.acknowledge(id)
	_tuning.set_value(id, value)


## Build a session against this circuit and start it. Issue #180.
##
## `test_track.gd`'s `_build_session` is the same shape and this is deliberately not
## a shared helper yet: the two differ in exactly the places that matter — the track
## slug and its hash, the layout, and the fact that the runner is handed a
## `KartTrack` — and merging them before a third scene exists would be guessing at
## which of those three is the variable one. #183 is where the shared rig goes.
func _build_session() -> void:
	if _kart == null or _driver == null:
		return

	_session = KartSession.new()
	# The slug is the file's stem, so a lap time is filed under the circuit rather
	# than under the scene that drove it.
	_session.set_track(_track.source_path().get_file().get_basename().get_basename())
	# **And its content hash, which `test_track` cannot have.** That scene builds its
	# geometry in code and leaves the hash at zero rather than inventing one; this one
	# has a file, so ADR-0041's guarantee is available and taking it is one line: a
	# replay recorded before a corner was smoothed refuses to play back and names the
	# file that moved.
	_session.set_track_hash_hex(_track.content_hash())
	_session.set_layout(
		KartSession.LAYOUT_REVERSE if _track.is_reversed() else KartSession.LAYOUT_FORWARD
	)
	_session.set_condition(KartSession.CONDITION_DRY)
	_session.set_kart_class(KartSession.CLASS_KZ2)

	var requested := Cmdline.as_string(_args, "session", "practice")
	if not SESSION_TYPES.has(requested):
		push_error("--session=%s is not one of %s" % [
			requested, ", ".join(PackedStringArray(SESSION_TYPES.keys())),
		])
		requested = "practice"
	_session.set_type(SESSION_TYPES[requested])
	# The scheduled limit rather than a typed number: *"a Heat's distance is
	# arithmetic on a sourced FIA figure and typing 3750 into a menu is how that
	# citation gets lost."*
	_session.use_scheduled_limit()
	if _args.has("laps"):
		_session.set_limit(KartSession.LIMIT_LAPS, float(Cmdline.as_int(_args, "laps", 3)))

	# The assists as the kart already has them, so `--auto-shift=off` is recorded in
	# the configuration rather than contradicted by it.
	_session.set_auto_clutch(_kart.auto_clutch)
	_session.set_auto_shift(_kart.auto_shift)
	_session.set_tick_hz(Engine.physics_ticks_per_second)
	if _tuning != null:
		_session.adopt_tuning(_tuning)

	_runner = SessionRunner.new()
	_runner.name = "SessionRunner"
	add_child(_runner)
	# **The `KartTrack` and not a `TrackLayout`.** It carries the authored sector
	# marks, the checkpoints and the per-station width; `SessionRunner.configure()`
	# names the seven methods and which three are optional.
	if not _runner.configure(_session, _kart, _driver, _track):
		push_error("session refused: %s" % _runner.refusal())
		_runner = null
		return
	_runner.session_finished.connect(_on_session_finished)
	_runner.begin()


## The result, printed. There is no results screen — `GAMEDESIGN.md` §9 puts that
## behind #171 — so the terminal is where a session's result goes, as one sentence.
func _on_session_finished(_result: Dictionary) -> void:
	print(_runner.result_line())


func _build_cameras() -> void:
	_chase = ChaseCamera.new()
	_chase.name = "ChaseRig"
	add_child(_chase)
	_chase.set_target(_kart)
	_chase.camera.attributes = LookEnv.camera_attributes(_args)
	# Further than the proving ground's default: from the start straight the far
	# side of a 1,375 m loop with a 402 m bounding box is a long way away.
	_chase.camera.far = 2500.0

	_cockpit = CockpitCamera.new()
	_cockpit.name = "CockpitRig"
	_kart.add_child(_cockpit)
	_cockpit.set_target(_kart)
	_cockpit.camera.attributes = LookEnv.camera_attributes(_args)
	_cockpit.camera.far = 2500.0

	_free = FreeCamera.new()
	_free.name = "FreeCamera"
	_free.attributes = LookEnv.camera_attributes(_args)
	add_child(_free)

	var eye := Cmdline.as_string(_args, "eye", "")
	if eye == "":
		return
	_fixed = Camera3D.new()
	_fixed.name = "FixedCamera"
	_fixed.fov = Cmdline.as_float(_args, "fov", 55.0)
	_fixed.near = 0.05
	_fixed.far = 2500.0
	_fixed.attributes = LookEnv.camera_attributes(_args)
	add_child(_fixed)
	var target := _parse_point(Cmdline.as_string(_args, "look", ""), _spawn.origin)
	var eye_point := _parse_point(eye, _spawn.origin + Vector3(8.0, 3.0, 26.0))
	# An overhead still points the camera straight down, where `Vector3.UP` is
	# colinear with the view direction: Godot warns and then picks a roll of its
	# own, so two runs of the same command frame the circuit two ways — which breaks
	# the one rule every still in this project is held to.
	var up := Vector3.UP
	if absf((target - eye_point).normalized().dot(Vector3.UP)) > 0.999:
		up = Vector3.FORWARD
	_fixed.look_at_from_position(eye_point, target, up)


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
	_hud.add_theme_font_size_override("font_size",
			maxi(15, int(round(20.0 * get_viewport().get_visible_rect().size.y / 1080.0))))
	_hud.add_theme_color_override("font_color", Color.WHITE)
	_hud.add_theme_color_override("font_outline_color", Color.BLACK)
	_hud.add_theme_constant_override("outline_size", 5)
	_hud.position = Vector2(14, 12)
	layer.add_child(_hud)

	_driving_hud = preload("res://scripts/ui/driving_hud.gd").new()
	_driving_hud.name = "DrivingHud"
	layer.add_child(_driving_hud)
	if _kart != null:
		_driving_hud.bind(_kart)

	# The timing screen. Three overlays and not one, because they answer different
	# questions — `driving_hud.gd`'s header is emphatic about not becoming the panel
	# it already refused to become once. Its sector strip is `sector_count()` wide,
	# which on this circuit is the file's three and not a constant.
	if _runner != null:
		_timing_hud = TimingHud.new()
		_timing_hud.name = "TimingHud"
		layer.add_child(_timing_hud)
		_timing_hud.bind(_runner)

	if _tuning != null:
		_tuning_panel = TuningPanel.attach(self)


## What the circuit is, printed once, so a driving session starts with the numbers
## the corners were sized from rather than with a shape on a screen.
func _report() -> void:
	var measured: Dictionary = _track.measurements()
	print("%s — %s layout, %.2f m, %d corners, content %s" % [
		_track.track_name(), _track.layout(), _track.length(),
		_track.corner_count(), _track.content_hash(),
	])
	print("  longest straight %.1f m at %.1f   start line to T1 %.1f m   last corner to line %.1f m" % [
		measured["longest_straight"], measured["longest_straight_at"],
		measured["start_to_first_corner"], measured["last_corner_to_start"],
	])
	print("  elevation %.2f m, low %.2f at %.1f, high %.2f at %.1f   width %.1f-%.1f m" % [
		measured["elevation_range"], measured["elevation_low"], measured["elevation_low_at"],
		measured["elevation_high"], measured["elevation_high_at"],
		measured["min_width"], measured["max_width"],
	])
	print("  closest approach %.2f m of clear ground, worst ground slope %.2f%%" % [
		measured["min_clear_ground"], measured["worst_ground_slope_pct"],
	])
	var parts := PackedStringArray()
	for surface_name in _triangles:
		parts.append("%s %d" % [surface_name, _triangles[surface_name]])
	print("  collision triangles: %s" % ", ".join(parts))
	print("  sector marks %s   %d checkpoints   %d grid slots" % [
		_track.sector_marks(), _track.checkpoints().size(), _track.grid_count(),
	])
	# What the timer was actually begun with, rather than what the file holds — the
	# two lists are merged into one ordered set and the counts are the cheapest place
	# a merge that dropped something would show up.
	if _runner != null:
		var timer := _runner.timer()
		print("  %s: %d sectors over %d marks, timed at %d Hz" % [
			_session.type_name(_session.get_type()), timer.sector_count(),
			timer.mark_count(), Engine.physics_ticks_per_second,
		])
	# The controls, named, and the pad named if one is plugged in. A driver holding
	# a DualSense was once shown W/S and A/D and closed the window without driving.
	for line in ControlHints.lines():
		print(line)


# --- running ---------------------------------------------------------------


func _process(_delta: float) -> void:
	if _hud != null:
		_hud.text = _hud_text()
	_rig.update_wheel_visuals()


func _physics_process(_delta: float) -> void:
	if _kart == null:
		return
	# One hinted projection a tick. The hint is not an optimization — it is what
	# tells the two ends of Il Pozzo apart, and the hairpin's own two tangents are
	# 30 m apart in plan.
	var placed := _track.project(_kart.global_position, _hint)
	_hint = placed["distance"]
	var report: Array = _kart.wheel_report()
	var surfaces := {}
	for wheel in report:
		var type := int(wheel.get("surface_type", 0))
		surfaces[type] = int(surfaces.get(type, 0)) + 1
	var pieces := PackedStringArray()
	for type in surfaces:
		pieces.append("%d %s" % [surfaces[type], SURFACE_NAMES[clampi(type, 0, 3)]])
	_surface_text = ", ".join(pieces)


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("respawn"):
		_respawn()
	elif event.is_action_pressed("camera_cycle"):
		_set_camera_mode(_next_camera_mode())


## Back to the nearest point of the racing surface, facing the way the layout is
## driven — not back to the grid.
##
## A 1,375 m circuit is long enough that respawning at the start line after a spin
## at the hairpin is a 900 m drive back, so the useful respawn is the one that puts
## the kart where it left the road. The hint is deliberately **cleared** afterwards:
## a stale hint after a teleport confines the next search to a window the kart is no
## longer in and returns the nearest point of it with every appearance of success.
func _respawn() -> void:
	if _kart == null:
		return
	var placed := _track.project(_kart.global_position, _hint)
	var frame := _track.sample(placed["distance"])
	var heading: float = frame["heading"]
	var position: Vector3 = frame["position"]
	_kart.set_spawn(Transform3D(Basis(Vector3.UP, -heading),
			position + Vector3(0.0, KartRig.SPAWN_LIFT, 0.0)))
	_kart.respawn()
	_hint = -1.0
	# The timer has to be told, and it has to be told *after* the body has moved: it
	# re-arms its marks from where the kart was put down, so every mark ahead is still
	# owed and a respawn past a checkpoint costs the lap rather than skipping it for
	# free. It also throws away its own stale hint, which is exactly what has just
	# been invalidated here.
	if _runner != null:
		_runner.notify_respawn()


func _set_camera_mode(mode: String) -> void:
	_camera_mode = mode
	if _fixed != null:
		_fixed.current = true
		return
	if _chase != null:
		_chase.camera.current = mode == "chase"
	if _cockpit != null:
		_cockpit.camera.current = mode == "cockpit"
	if _free != null:
		_free.current = mode == "free"
		_free.set_process_input(mode == "free")
		if mode == "free" and _kart != null:
			_free.global_transform = _chase.camera.global_transform
	# The flight camera reuses the drive keys — `free_camera.gd` reads `throttle`,
	# `brake` and the two steer actions — so a driver flying the view would otherwise
	# be driving the kart at the same time. Since #180 there is a runner here, and it
	# is the single owner of `PlayerDriver.enabled` per ADR-0040: this asks it rather
	# than writing the flag, so a camera cannot hand the throttle back to a kart the
	# runner is still holding.
	_suspend_input(mode == "free")


## One writer of `PlayerDriver.enabled`, and it is the runner when there is one. The
## direct write survives for the case where there is not — a refused session — since
## a free camera that could not stop the kart would be a flight control that drives.
func _suspend_input(suspended: bool) -> void:
	if _runner != null:
		_runner.set_input_suspended(suspended)
	elif _driver != null:
		_driver.enabled = not suspended


func _next_camera_mode() -> String:
	match _camera_mode:
		"chase": return "cockpit"
		"cockpit": return "free"
		_: return "chase"


func _hud_text() -> String:
	if _kart == null:
		return ""
	var placed := _track.project(_kart.global_position, _hint)
	var corner_name := ""
	for index in _track.corner_count():
		var corner := _track.corner(index)
		var from: float = corner["from"]
		var to: float = corner["to"]
		var here: float = placed["distance"]
		var inside := here >= from and here <= to if from <= to else (here >= from or here <= to)
		if inside:
			corner_name = "%s  %s  line %.1f m  apex %.0f km/h" % [
				corner["name"], corner["hand"], corner["line_radius"], corner["apex_kmh"],
			]
			break
	return "%s — %s\n%.0f m  %+.1f m off line  %s\n%s" % [
		_track.track_name(), _track.layout(),
		placed["distance"], placed["lateral"], _surface_text, corner_name,
	]
