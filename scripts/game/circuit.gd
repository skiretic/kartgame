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
##   --scatter=true      the trackside dressing, placed from the file
##   --terrain=true      ground that follows the circuit's elevation; false
##                       restores the flat slab this scene shipped with
##   --terrain-cell=5.0  meters per terrain grid cell
##   --gi=none           none or baked. **Default none, on purpose** — see
##                       `_build_lightmap`. `baked` consumes valdirone.lmbake and
##                       turns sky ambient off, and blows every dynamic object in
##                       the scene out to white
##   --scatter-gi=off    off or probes, and only under `--gi=baked`
##   --probe=true        the reflection probe; false is for A/B work
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
## ## The ground, and why it stopped being flat
##
## Issue #182. This scene used to put one flat `PlaneMesh` under the whole world at
## the circuit's lowest point, which is invisible while the only thing standing on
## it is the road — the road stands on its own spline. Valdirone climbs 12.55 m, so
## at the top of the climb the asphalt and its run-off were a ribbon hanging
## 12.05 m in the air over a lawn, and the first prop placed beside the track had
## its roots in the sky. `TrackTerrain` replaces it with a height field taken from
## the circuit itself, and the old slab stays underneath as the backstop it always
## was. `--terrain=false` restores the flat version, which is what to use if a
## measurement ever has to be compared against a still taken before this.
##
## ## What it deliberately does not do yet
##
## **The scatter has no colliders.** `TrackScatter`'s header has the argument: a
## kart that clears the barrier drives through a tree. The barrier is the collider
## that matters and adding more static bodies beside a racing line is a way to move
## a measured figure for a decoration.
##
## **The pit lane's asphalt is built** since issue #181, and it needs no call from
## here: `_build_track()`'s loop over `surface_meshes()` picks the `PitLane` entry
## up like any other surface and gives it its own `StaticBody3D`. One shared lane
## plus four per-layout gores, because reversed, a deceleration lane at 20° to the
## direction of travel is a 160° merge over Part I **§7.2**'s 30° cap — §7.2, not
## §7.4, which is where five places in this repo had it. `KartTrack.pit_lane()` and
## `pit_stubs()` publish the stations for anything that wants them.

const DEFAULT_TRACK := "res://data/tracks/valdirone_nuova.track.json"
const TRACK_MESH_PATH := "res://assets/generated/valdirone_nuova.glb"

## What `tools/bake/bake.sh --scene=res://scenes/game/valdirone_bake.tscn` writes.
##
## The bake happens in that scene and is consumed in this one, and the join is a
## node path: `LightmapGIData` stores its users as paths relative to the
## `LightmapGI`'s parent — `GroundVisual`, `TrackMesh/Asphalt` and so on — so the
## two scenes have to name their geometry identically. They do, because both build
## it from the same two places. That is the whole contract, and it is fragile
## enough to be worth stating: rename `GroundVisual` and the ground silently stops
## being lit.
const LIGHTMAP_PATH := "res://scenes/game/valdirone.lmbake"

## The pause menu, loaded by path rather than `preload`ed. A `--script` gate
## populates no class cache, and a hard reference to a screen that has not been
## imported yet is a parse error that takes the whole scene down rather than a
## session that runs without a pause menu.
const PAUSE_SCREEN := "res://scripts/shell/screens/pause_screen.gd"

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
var _corridor: TrackCorridor
var _terrain: TrackTerrain
var _scatter: TrackScatter
var _build_ms := {}
var _lightmap_users := 0

## The ghost, recording and playing back. ADR-0041, ROADMAP M3c.
##
## `KartGhost`, `ghost_kart.gd` and `ghost_probe.gd` were all built and complete
## and **no scene instantiated any of them** — the same shape as the profile hole
## this milestone exists to close. This is the join, and it is a join rather than
## a build: every method used below already existed and is already gated by
## `ghost_probe.gd`'s 23 checks.
var _ghost_recorder: KartGhost
var _ghost_kart: GhostKart
var _ghost_saved_id := ""
var _ghost_saved_s := -1.0
## The time to beat before a recording is worth keeping. Seeded from the profile
## so a slow session cannot overwrite a faster stored ghost with its own best —
## `save_as_id()` derives one filename per track+layout+class, so an unguarded
## save would leave the profile's record pointing at a ghost slower than the time
## written beside it.
var _ghost_target_s := -1.0
var _pause_stack: ScreenStack


## What `_ready()` builds, in order, so a loading screen can name the step it is
## on instead of animating a bar that means nothing.
##
## **The steps are real and the timings are already collected.** `_build_ms`
## has always held the per-stage cost; nothing published it. Do not invent the
## mockup's six pretty names on top of this list -- `circuit.gd` builds
## synchronously in `_ready()`, so the loading screen renders one frame and then
## the process sits here for the whole build, and a bar that claimed smooth
## progress through named stages would be the one lie on the screen.
const BUILD_STEPS: PackedStringArray = [
	"track", "environment", "ground", "collision", "scatter", "lightmap",
	"kart", "tuning", "session", "cameras", "hud",
]

## Emitted as each stage finishes, with the stage's own measured cost.
signal step_done(name: String, index: int, total: int, elapsed_ms: float)

## Emitted when whoever is driving asks to leave -- the pause menu's "Quit to
## paddock", or the flag falling. The shell listens; a headless run does not, and
## `_on_session_finished` still prints its sentence either way.
signal session_over(result: Dictionary)


func _ready() -> void:
	# `SessionRequest.take()` and not `Cmdline.parse()` alone. The merge puts the
	# shell's posted keys in first and the command line's in second, so **the
	# command line wins every key it carries** -- which is why drive.sh,
	# circuit.sh, bake.sh, session_probe.gd and every recorded shoot.sh
	# invocation are byte-identical after this line changed. With nothing posted
	# it is exactly `Cmdline.parse()`. See session_request.gd's header: the read
	# is destructive on purpose, so a stale configuration cannot survive into the
	# next session.
	_args = SessionRequest.take(Cmdline.parse())
	_step_started = Time.get_ticks_usec()

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

	_step("track")

	_build_environment()
	_step("environment")
	_build_ground()
	_step("ground")
	_build_track()
	_step("collision")
	# After the ground, because every prop stands on the terrain's height field and
	# before the kart, so a still taken with `--eye` frames a dressed circuit rather
	# than one that gains its trees a frame later.
	_build_scatter()
	_step("scatter")
	# After every mesh it lights exists. `LightmapGI` binds its data on entering the
	# tree, so one added before the geometry finds nothing, warns that it needs a
	# rebake, and leaves the scene unlit — `bake_test.gd` documented this ordering
	# and it applies to every procedurally built scene in the project.
	_build_lightmap()
	_step("lightmap")
	_build_kart()
	_step("kart")
	_build_tuning()
	_step("tuning")
	# After the tuning, because `adopt_tuning` records what the session was driven
	# under and the `--tune`/`--preset` arguments have been applied by then; before
	# the HUD, because the timing screen binds to the runner and a HUD built first
	# would bind to null and draw nothing all session.
	_build_session()
	_step("session")
	_build_cameras()
	_step("cameras")
	_build_hud()
	add_child(preload("res://scenes/ui/telemetry.tscn").instantiate())
	_step("hud")

	_build_pause_host()

	_set_camera_mode(Cmdline.as_string(_args, "camera", "chase"))
	_report()


## Somewhere for the pause menu to be pushed. ADR-0052, mockup plate 8.
##
## **Without this the pause screen is unreachable and Practice has no exit.**
## `pause` has been in `project.godot` since M0 and `grep` found it read by
## nothing but `input_probe.gd`'s list — the fourth member of the family
## `control_hints.gd` heads: a control that is bound, advertised and unread.
## Practice is `LIMIT_OPEN` and `SessionRunner.end_session()` is its only ending,
## so a session with no pause menu is a session you can only leave by closing the
## window, and the results sheet is unreachable by construction.
##
## A `ScreenStack` here rather than in the shell, because the shell is not in the
## tree during a session — `start_session()` swapped the whole scene. The stack's
## `shell_root` is deliberately **null**: there is no `ShellRoot` to hand out, and
## `pause_screen.gd` already reads its own `KartSettings` off disk for exactly
## that case.
##
## `get_tree().paused` is not used and the layer is not `PROCESS_MODE_ALWAYS`.
## The world keeps running behind the veil on purpose — the kart is not frozen,
## the session clock keeps ticking, and the pause screen's consequence line says
## so in as many words.
func _build_pause_host() -> void:
	if not ResourceLoader.exists(PAUSE_SCREEN):
		return
	var layer := CanvasLayer.new()
	layer.name = "PauseUI"
	# Above the driving HUD and the timing screen, which build their own layers.
	layer.layer = 10
	add_child(layer)
	_pause_stack = ScreenStack.new()
	# This stack starts empty and its depth 1 is the pause menu itself, so the
	# shell's "the bottom screen never pops" rule would make Resume impossible.
	_pause_stack.keeps_bottom = false
	layer.add_child(_pause_stack)


## The arguments this session was built from, so "Restart session" can re-post
## them. `SessionRequest.take()` was destructive, so nothing else still holds
## them — which is why the pause screen takes them by hand rather than reading
## them back out of somewhere.
func session_config() -> Dictionary:
	return _args.duplicate()


## Open the pause menu, or close it. One key does both: `menu_back` and `pause`
## are both Escape, and `ScreenStack._input()` returns without consuming when its
## stack is empty — so a closed menu lets the key through to here and an open one
## eats it and pops itself.
func _toggle_pause() -> void:
	if _pause_stack == null:
		return
	if _pause_stack.depth() > 0:
		_pause_stack.back()
		return
	var script := load(PAUSE_SCREEN) as GDScript
	if script == null:
		return
	var screen := script.new() as ShellScreen
	_pause_stack.push(screen)
	# After the push, because `bind_session` rebuilds the rows and `build()` has
	# to have run first. `on_enter` is what gates the input, a frame later, so the
	# driver is not suspended before the screen exists.
	if screen.has_method("bind_session"):
		screen.call("bind_session", self, session_config())


# --- construction ----------------------------------------------------------


## Close out a build stage: record what it cost and tell whoever is listening.
##
## The clock is wall time between calls rather than a timer per builder, which is
## why `_build_ms` keeps its own three finer-grained entries — `corridor`,
## `terrain` and `scatter` are measured inside the functions that own them and
## reported in `_report()`. These are the coarse steps a loading screen names.
## Seeded in `_ready()` before the first `_step()`, **not** lazily inside it.
##
## Seeding it on first call makes the first step measure the interval from itself
## to itself, so `track` reported 0.00 ms forever while `KartTrack.load()` plus
## `select_layout()` actually costs about 3.2 ms. A stage that always reports zero
## reads as a stage that costs nothing, which is the wrong thing to learn from a
## table whose whole purpose is deciding what to make asynchronous.
var _step_started := 0


func _step(step_name: String) -> void:
	var now := Time.get_ticks_usec()
	var elapsed := (now - _step_started) / 1000.0
	_step_started = now
	_build_ms["step_" + step_name] = elapsed
	var index := BUILD_STEPS.find(step_name)
	step_done.emit(step_name, index, BUILD_STEPS.size(), elapsed)


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
	if not Cmdline.as_bool(_args, "probe", true):
		return
	var extent := _world_extent()
	var centre := _bounds().get_center()
	add_child(LookEnv.reflection_probe(
		Vector3(extent, 120.0, extent),
		Vector3(centre.x, centre.y + 20.0, centre.z),
		extent
	))


## The ground: a height field taken off the circuit, over the slab that was here
## before. Both are `surface_type` 2.
##
## A field with a circuit-shaped hole in it would be the physically honest shape
## and it is a great deal of geometry for nothing — the road stands above this and
## wins every suspension raycast that lands on it.
##
## **Sunk to the circuit's lowest point rather than to y = 0**, which the test
## track does not have to think about because it is flat. Valdirone's hairpin sits
## 10.8 m below the start line; a slab at zero would put the bottom of the bowl
## underground and the kart would drive into a hillside made of grass.
##
## The slab is *kept* rather than replaced. `TrackTerrain` builds a surface
## collider and a surface has one failure mode a volume does not — 2 m of slab
## cannot be passed through in one tick at any speed this kart reaches, and this
## circuit puts one into a kerb at 129 km/h by design. It is sunk
## `TrackTerrain.BASE_CLEARANCE` below the field's own base so the two are never
## coplanar, which is the condition that makes a wheel raycast's answer arbitrary
## along a whole boundary.
func _build_ground() -> void:
	var box := _bounds()
	var extent := _world_extent()
	var centre := box.get_center()
	var floor_y := box.position.y - 0.5

	var started := Time.get_ticks_usec()
	_corridor = TrackCorridor.new()
	_corridor.measure(_track)
	_build_ms["corridor"] = (Time.get_ticks_usec() - started) / 1000.0

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

	var slab_top := floor_y
	if Cmdline.as_bool(_args, "terrain", true):
		started = Time.get_ticks_usec()
		_terrain = TrackTerrain.new()
		_terrain.build(_track, _corridor, extent, centre, floor_y,
				Cmdline.as_float(_args, "terrain-cell", 5.0))
		_build_ms["terrain"] = (Time.get_ticks_usec() - started) / 1000.0
		add_child(_terrain.visual())
		add_child(_terrain.body(2))
		slab_top = _terrain.base_y()
	else:
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

	var shape := BoxShape3D.new()
	shape.size = Vector3(extent, GROUND_THICKNESS, extent)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.position = Vector3(centre.x, slab_top - GROUND_THICKNESS * 0.5, centre.z)
	body.add_child(collider)
	add_child(body)


## The trackside dressing. Issue #182.
##
## Placement is `TrackScatter`'s and every object is a function of the file, so a
## corner that moves takes its trees with it. Nothing here carries a collider —
## that argument is in `TrackScatter`'s header and in this script's own.
func _build_scatter() -> void:
	if not Cmdline.as_bool(_args, "scatter", true):
		return
	if _terrain == null:
		# Without the height field there is nothing to stand a prop on: the flat
		# slab is up to 12.05 m below the road, so scatter placed at road level
		# floats and scatter placed on the slab is buried under the run-off.
		push_warning("--scatter needs --terrain; nothing placed")
		return
	var started := Time.get_ticks_usec()
	_scatter = TrackScatter.new()
	_scatter.generate(_track, _corridor, _terrain)
	_scatter.build(self)
	_build_ms["scatter"] = (Time.get_ticks_usec() - started) / 1000.0


## The baked lightmap, if one has been made and if it is asked for. Issue #182.
##
## **Default off, and that is a measurement rather than caution.** The bake itself
## works — `tools/bake/bake.sh --scene=res://scenes/game/valdirone_bake.tscn`
## produces it in 38 s and the static geometry renders correctly from it. What
## does not work is putting anything that *moves* in the same scene: a dynamic
## object takes its indirect light from the `.lmbake`'s probe field, that field is
## stored as unnormalized physical radiance, and under physical light units it
## arrives about four orders of magnitude too bright. Every kart, and all 5,187
## scatter instances, render clipped to white.
##
## `LightmapGI.camera_attributes` is the only lever on baked exposure and it does
## not reach the probes: baking with it set moved the *texture* path (the far band
## at the reference camera from 96.0 to 126.6 mean channel value) and left the
## probe path **byte-identical** at 253.9. So no setting of it makes both paths
## right. `docs/adr_pending_182.md` has the full table.
##
## The node is created here rather than sitting in `valdirone.tscn` because the
## data is loaded rather than referenced: an `ext_resource` to a `.lmbake` would
## make the scene fail to load on a checkout where nobody has run the bake, which
## is the same argument the scene file already makes about the two `.glb` files.
##
## **Sky ambient goes off when the lightmap goes on**, and that is ADR-0022 rather
## than a look choice: the bake already contains the sky, so leaving
## `AMBIENT_SOURCE_SKY` on counts it once in the lightmap and once live. It is the
## third of the four ambient double-counts this project has found and the only one
## that arrives with a lightmap. `LookEnv` is not changed — every scene without a
## lightmap still wants sky ambient, and this is the only scene with one.
func _build_lightmap() -> void:
	if Cmdline.as_string(_args, "gi", "none") != "baked":
		return
	if not ResourceLoader.exists(LIGHTMAP_PATH):
		# Not a warning. A checkout that has never run the bake is the normal case,
		# and the scene is complete without it.
		return
	var data := load(LIGHTMAP_PATH) as LightmapGIData
	if data == null:
		push_warning("%s did not load as LightmapGIData" % LIGHTMAP_PATH)
		return
	# **Before the node is added, and it is the fifth ambient double-count.**
	#
	# `LightmapGI` binds its data to a mesh by node path and does not care what
	# that mesh's `gi_mode` is. `GI_MODE_DYNAMIC` — the default, and what the
	# glTF importer leaves on the track mesh at `light_baking=1` — means "take
	# indirect light from probes", and the `.lmbake` carries probe data. So a mesh
	# on the default gets the baked lightmap *and* the probe field that was baked
	# from the same light, and the two add. Measured on the ground at this camera:
	# 226.1/255 with `GI_MODE_DYNAMIC` against 38.4 unlit, where the same lightmap
	# in `valdirone_bake.tscn` — which sets the mode — renders 64.1. Blown to
	# white, from one property nobody set.
	#
	# CLAUDE.md's lighting note predicts a fifth of these and says to assume it.
	# This is it, and it is the first one that only appears when a bake exists.
	for node in _static_lit_meshes():
		node.gi_mode = GeometryInstance3D.GI_MODE_STATIC

	# **The scatter's GI is switched off when a lightmap is present, and the
	# measurement that forced it is worth keeping.**
	#
	# `MultiMeshInstance3D` cannot be baked, so the scatter is `GI_MODE_DYNAMIC`
	# and takes its indirect light from the `.lmbake`'s probe field — which is what
	# probes are for and is the arrangement `docs/adr_pending_182.md` argues for.
	# Rendered, it blows the whole frame to white. Measured at this camera, mean
	# channel value over the near-ground third: 46.8 with no lightmap, 53.8 with
	# the lightmap and no scatter, and **210.6** with both. The far band goes from
	# 89.1 to 253.9, which is clipped. Turning off the reflection probe changes
	# none of those three numbers, so it is not that; removing the scatter fixes
	# all of them, so it is the scatter.
	#
	# `GI_MODE_DISABLED` is a stop, not the fix — it costs the foliage its bounce
	# light, so a tree in the shade is lit by nothing. The real answer is either
	# the probe scaling or dropping the `MultiMesh`, and both are more than this
	# ticket. `--scatter-gi=probes` reproduces the failure.
	var scatter_root := get_node_or_null("Scatter")
	if scatter_root != null and Cmdline.as_string(_args, "scatter-gi", "off") == "off":
		for node in scatter_root.get_children():
			(node as GeometryInstance3D).gi_mode = GeometryInstance3D.GI_MODE_DISABLED

	var lightmap := LightmapGI.new()
	lightmap.name = "LightmapGI"
	lightmap.light_data = data
	add_child(lightmap)
	_lightmap_users = data.get_user_count()

	var world_environment := get_node_or_null("WorldEnvironment") as WorldEnvironment
	if world_environment != null:
		world_environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED


## Exactly the meshes the `.lmbake` names as its users: the ground and the
## generated track. **Not the scatter** — it is `MultiMeshInstance3D`, which the
## baker never saw, so it stays `GI_MODE_DYNAMIC` and takes its indirect light
## from the lightmap's probes. That is the intended arrangement, not an omission.
func _static_lit_meshes() -> Array[MeshInstance3D]:
	var found: Array[MeshInstance3D] = []
	var ground := get_node_or_null("GroundVisual") as MeshInstance3D
	if ground != null:
		found.append(ground)
	var mesh := get_node_or_null("TrackMesh")
	if mesh != null:
		for node in mesh.find_children("*", "MeshInstance3D", true, false):
			found.append(node as MeshInstance3D)
	return found


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
	_runner.lap_completed.connect(_on_lap_completed)
	_build_ghost()
	_runner.begin()


## Record this session's laps, and play back the stored best if one was asked for.
##
## `--ghost=off` is the default for a scripted run and `--ghost=best` turns
## playback on, because a `shoot.sh` still or a `drive.sh` scenario must not gain
## a second kart in frame from a file sitting in `user://`. Recording is always
## on: it costs one `record_tick` a physics tick, it is what makes a first lap
## worth setting, and nothing that reads a gate can see it.
func _build_ghost() -> void:
	_ghost_recorder = KartGhost.new()
	if not _ghost_recorder.adopt_session(_session):
		push_warning("ghost: adopt_session refused; not recording")
		_ghost_recorder = null
		return
	_ghost_recorder.begin_record()

	var profile := KartProfile.new()
	profile.load()
	var track_id := _session.get_track()
	var layout := _session.get_layout()
	var kart_class := _session.get_kart_class()
	if profile.has_best(track_id, layout, kart_class):
		_ghost_target_s = profile.best_time(track_id, layout, kart_class)

	if Cmdline.as_string(_args, "ghost", "off") != "best":
		return
	var id := profile.best_ghost_id(track_id, layout, kart_class)
	if id.is_empty():
		# Said rather than silently skipped: "ghost: best" with no ghost on file is
		# a reasonable thing to ask for on a first visit, and a driver who asked
		# and got nothing should be told which of the two happened.
		print("ghost: none on file for %s %s" % [track_id, _track.layout()])
		return
	var stored := KartGhost.new()
	var loaded: Dictionary = stored.load_id(id)
	if not bool(loaded.get("ok", false)):
		print("ghost: %s did not load (%s)" % [id, loaded.get("reason", "no reason")])
		return
	_ghost_kart = GhostKart.new()
	_ghost_kart.name = "GhostKart"
	add_child(_ghost_kart)
	_ghost_kart.set_ghost(stored)
	print("ghost: %s, %s over %d samples" % [
		id, SessionRunner.format_time(stored.lap_time()), stored.sample_count(),
	])


## One lap closed. Keep the recording if it is worth keeping, then start again.
##
## `adopt_lap()` does the work and it does the refusing: it reads the timer's last
## lap, **discards the recording outright when that lap was invalid** — a ghost of
## a lap that cut a corner is a ghost driving through the scenery — and otherwise
## closes it with the lap's real sector durations. So validity is not re-decided
## here; it is asked for once, in the place that owns it.
func _on_lap_completed(number: int, time_s: float, valid: bool, _reason: String) -> void:
	if _ghost_recorder == null:
		return
	var adopted: Dictionary = _ghost_recorder.adopt_lap(_runner.timer())
	if bool(adopted.get("ok", false)) and _beats_target(time_s):
		if _ghost_recorder.save_as_id() == OK:
			_ghost_saved_id = _ghost_recorder.id()
			_ghost_saved_s = time_s
			_ghost_target_s = time_s
			print("ghost: lap %d saved as %s (%s)" % [
				number, _ghost_saved_id, SessionRunner.format_time(time_s),
			])
		else:
			push_warning("ghost: lap %d closed but save_as_id refused" % number)
	# Whether it was kept, refused as invalid or simply slower, the next lap gets
	# a clean recorder. `discard()` is a no-op on an already-discarded one.
	_ghost_recorder.discard()
	_ghost_recorder.begin_record()
	if not valid:
		return


## Strictly faster, and quantized the way the profile quantizes, so a lap that is
## a nanosecond quicker does not rewrite a 45 KB file and claim an improvement
## nobody could drive.
func _beats_target(time_s: float) -> bool:
	if not (time_s > 0.0):
		return false
	if _ghost_target_s <= 0.0:
		return true
	return int(round(time_s * 1e6)) < int(round(_ghost_target_s * 1e6))


## The result, printed **and** handed to the shell.
##
## The `print` stays, and it is not redundant. Every headless run in this project
## reports its session this way — `session_probe.gd`, `drive.sh`, a bare
## `godot --path . scenes/game/valdirone.tscn` — and a results screen appearing
## does not make a terminal stop being where a scripted run says what happened.
##
## `SessionRequest.deliver()` carries what the sheet needs and the runner does
## not have: the per-lap ledger, because `KartLapTimer` publishes only `last_*`
## and `best_*` and never a history, and the circuit's display name, because the
## runner deals in a slug and a classification has a masthead.
func _on_session_finished(result: Dictionary) -> void:
	print(_runner.result_line())
	var carried := result.duplicate(true)
	carried["track_name"] = _track.track_name()
	carried["layout_name"] = _track.layout()
	carried["result_line"] = _runner.result_line()
	carried["laps"] = _ledger_rows()
	# The ghost this session actually wrote, so the profile write can point the
	# `best` record at it. Empty when no lap beat what was already on file, which
	# is also the honest answer: the stored ghost is still the right one.
	carried["ghost_id"] = _ghost_saved_id
	carried["ghost_lap_s"] = _ghost_saved_s
	# **The identity, without which no lap can ever be filed.**
	# `SessionRunner.result()` publishes the type, the config hash and
	# `DriverResult`'s three measurements, and nothing that says which circuit
	# they were set on. `track_name` and `layout_name` above are display strings
	# for a masthead — "Valdirone Nuova" is not a slug and `best_lap_store` refuses
	# an empty one rather than filing under it.
	#
	# `track` and not `track_id`, because that is the key
	# `best_lap_store.record()` reads and the key `shell_probe.gd` passes it. One
	# spelling, and it is this one.
	carried["track"] = _session.get_track()
	carried["layout"] = _session.get_layout()
	carried["kart_class"] = _session.get_kart_class()
	SessionRequest.deliver(carried)
	session_over.emit(carried)


## The per-lap rows for the results sheet. Empty until `scripts/game/lap_ledger.gd`
## is attached — it subscribes to `SessionRunner.lap_completed` and reads
## `timer().last_sectors()` **inside the handler**, which is the only place it is
## still this lap's and not the next one's.
func _ledger_rows() -> Array:
	var ledger := get_node_or_null("LapLedger")
	return ledger.rows() if ledger != null else []


## Pause: gate the input, leave the world running.
##
## ADR-0052 and mockup plate 8, and both are emphatic that **the kart is not
## frozen**. `get_tree().paused` is deliberately not used — the session clock
## keeps running because `_tick_running()` keeps ticking, which is the whole
## reason the pause screen has a consequence line to write.
##
## The lever is `PlayerDriver.enabled`, and it keeps its single writer:
## `SessionRunner._apply_input_gate()`, reached through `set_input_suspended()`,
## whose docstring in `session_runner.gd:458` names this caller in advance. Pause
## does not become a second writer, and `set_input_as_handled()` is not the lever
## because `KartBody` polls the `Input` singleton in `_physics_process` and a
## consumed event does not reach that at all.
func set_paused(paused: bool) -> void:
	_suspend_input(paused)
	# The free camera polls the drive actions out of the `Input` singleton in its
	# own `_process`, so suspending the driver does not touch it. Its arrow-key
	# bindings are the menu's arrow keys, and one press of Down would otherwise
	# both walk the selection and fly the camera backwards.
	if _free != null:
		_free.enabled = not paused
	if paused:
		_strike_paused_lap()


## Strike the lap in flight, if the driver asked for that.
##
## **The host strikes it, not the pause screen.** The screen reads the same
## setting to say which of its two authored sentences to show, but a screen is a
## display and must not be the thing that decides a lap time. If the two ever
## disagree the screen is wrong and the record is right, which is the correct way
## round.
##
## Read from `KartSettings` here rather than taken from the screen for the same
## reason. Both read one source, so they agree by construction.
##
## `strike_paused()` drives `LapTimer::taint()` — the invalidation path that
## already existed — and touches no mark state and no travel counter, so it
## cannot produce the shape `lap_timing.h` argues against: a lap struck before its
## first mark that then never closes at all, leaving no lap on the screen and no
## reason.
func _strike_paused_lap() -> void:
	if _runner == null or not _runner.is_running():
		return
	if not ClassDB.class_exists("KartSettings"):
		return
	var settings := KartSettings.new()
	settings.load()
	if not settings.is_pause_invalidates_lap():
		return
	_runner.timer().strike_paused()


## Leave now. The pause menu's "Quit to paddock", and the only way out of a
## Practice session — `GAMEDESIGN.md` §4 gives Practice no limit, so nothing
## inside the simulation can end one and the decision belongs to whoever opened
## it. `end_session()` emits `session_finished`, so the result reaches the shell
## through `_on_session_finished` like any other ending.
func leave_session(outcome := "left for the paddock") -> void:
	if _runner != null and not _runner.is_over():
		_runner.end_session(outcome)
	ShellRoot.return_to_shell(get_tree())


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
	if _lightmap_users > 0:
		var modes := PackedStringArray()
		for node in _static_lit_meshes():
			modes.append("%s gi=%d" % [node.name, node.gi_mode])
		print("  lightmap: %d meshes lit, sky ambient off — %s" % [
			_lightmap_users, ", ".join(modes),
		])
	else:
		print("  lightmap: off — real-time GI, sky ambient on. --gi=baked to try it; "
			+ "issue #182 has why it is not the default")
	if _terrain != null:
		print("  terrain %d verts, %d tris, worst step %.3f m, built in %.0f ms" % [
			_terrain.vertex_count(), _terrain.triangle_count(),
			_terrain.roughness(), _build_ms.get("terrain", 0.0),
		])
	# The digest is the determinism claim, and it is printed on every launch rather
	# than kept in a gate: two runs of the same command that disagree here have
	# placed different scatter, and that is visible without running anything else.
	# `tools/verify/scatter_probe.gd` is the version that compares whole transforms.
	if _scatter != null:
		var counted := PackedStringArray()
		var placement: Dictionary = _scatter.placement()
		for key in placement:
			counted.append("%s %d" % [key, (placement[key] as Array).size()])
		print("  scatter %d objects (%s)" % [_scatter.total(), ", ".join(counted)])
		print("  scatter %d triangles over 7 MultiMesh draws, digest %s, placed in %.0f ms" % [
			_scatter.triangle_count(), _scatter.digest(), _build_ms.get("scatter", 0.0),
		])
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

	# One sample a physics tick, from the physics loop, because that is the clock
	# the pose is true on. `record_tick` decimates to the header's `sample_hz`
	# itself — feeding it at the sample rate instead makes the ghost store every
	# fourth pose while labelling them consecutive, and it plays back at four
	# times the speed it was recorded at.
	if _ghost_recorder != null and _ghost_recorder.is_recording():
		_ghost_recorder.record_tick(_kart.global_transform)

	# The ghost and the driver run on one clock — the lap clock — so the gap a
	# driver sees beside them is the gap the timing screen reports. Driven from
	# here rather than from `_process` so it cannot drift from the lap it belongs
	# to; `GhostKart` interpolates to frame rate on its own side.
	if _ghost_kart != null and _runner != null:
		_ghost_kart.playback_time = _runner.timer().lap_time()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("pause"):
		get_viewport().set_input_as_handled()
		_toggle_pause()
		return
	# Not while the menu is up. `respawn` is Circle, which is also `menu_back`, so
	# the stack has already consumed it — but `camera_cycle` is Create and reaches
	# here, and cycling to the free camera from behind a pause menu would hand the
	# throttle to a flight control the menu cannot see.
	if _pause_stack != null and _pause_stack.depth() > 0:
		return
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
