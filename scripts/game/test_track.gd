extends Node3D

## The test track: a closed 1,030 m loop built to be *judged by feel*.
##
##     godot --path . scenes/game/test_track.tscn
##     tools/shots/shoot.sh --scene=res://scenes/game/test_track.tscn --out=shots/track.png
##
## Arguments (all optional, after a bare `--`), plus everything `LookEnv` takes:
##
##   --curbs=true        the apex curbs, `surface_type` 1
##   --grass=true        the run-off, `surface_type` 2 — off makes the world asphalt
##   --markings=true     edge lines, start line, distance boards
##   --start=30          how far down the start/finish straight the grid sits, m
##   --camera=chase      which rig starts current: chase or free
##   --eye=x,y,z         park a camera here instead, and leave it there
##   --look=x,y,z        what the parked camera aims at (default: the grid)
##   --hud=true          the corner text overlay and the driving HUD
##   --steer-gamma=3.0   the steering input curve, through the tuning registry
##   --tune=key=value[,key=value]
##                       any tunable, by key. `tools/verify/tuning.sh` lists them
##   --preset=path       load a saved preset before the first tick
##   --validate=false    run the O(n^2) self-intersection check and print it
##   --throttle=1 --steer=0.3 --brake=0
##                       drive the kart from arguments instead of from the input
##                       map, so a still can be taken of a kart that is moving
##
## ## Why this exists next to the proving ground, which it does not replace
##
## `scenes/game/proving_ground.tscn` is a flat plane with a skidpad circle and a
## 200 m straight, deliberately featureless so a surprising kart has no surface to
## blame, and every §6.4 validation figure is measured on it. It stays exactly as
## it is.
##
## What it cannot do is let anyone judge the kart by feel, and four M3b tickets are
## blocked on exactly that — [#32](https://github.com/skiretic/kartgame/issues/32)
## "the inside rear visibly lifts",
## [#38](https://github.com/skiretic/kartgame/issues/38) "a launch requires clutch
## modulation", [#39](https://github.com/skiretic/kartgame/issues/39) "lifting off
## in second decelerates hard", and
## [#40](https://github.com/skiretic/kartgame/issues/40) "turning both assists off
## makes it demonstrably harder". This is the instrument those judgements get made
## on, and every corner on it is sized from a measured row of
## `tools/verify/drive_probe.gd`'s steering-lock sweep. `scripts/track/track_layout.gd`
## carries that table and the reasoning corner by corner; it is worth reading
## before driving, because two of the four corners are built to *fail* in specific
## ways and knowing which is the difference between a diagnosis and a surprise.
##
## It also closes the geometry half of two tickets. There is now a curb with a real
## vertical edge and `surface_type` = 1 — [#139](https://github.com/skiretic/kartgame/issues/139)
## says one has never existed anywhere in this project, which is why
## [#42](https://github.com/skiretic/kartgame/issues/42)'s "curbs are distinct from
## both asphalt and grass" is true in `src/core/surface.h`'s table and untested by
## anything that drives. The measured half of #139 — whether a wheel can tunnel
## through that edge at 100+ km/h — is still open; this only supplies the thing to
## drive at.
##
## **Geometry in code, not through Blender.** `tools/blender/genkart.sh` owns a
## determinism gate and a gitignored output path, and neither is right for a track
## that has to exist on a fresh clone. M5 builds the real pipeline from
## `track.json` (ARCHITECTURE.md §11); this is a small piece of it pulled forward
## the way #73's HUD was pulled forward from M6, and it deliberately does not touch
## that schema.
##
## ## What this scene owns that the kart does not
##
## The kart's **mesh and its collision shape**, and the wheel pivots. This section
## is copied from `proving_ground.gd` almost verbatim, which is deliberate:
## `src/vehicle/kart_body.h` says why it lives in the scene rather than in the
## node — asset plumbing rather than physics, and a C++ node that loads a
## gitignored `.glb` cannot be instantiated in a test — and two scenes doing it two
## different ways would be two karts.
##
## Not the lighting. `LookEnv` owns that, for the reason its own header gives at
## length: four ambient double-counting bugs so far, every one of them a scene that
## built its own.

## `kart::core::SurfaceType` from `src/core/surface.h`. **These integers are a wire
## format** — that header says so — so they are written out rather than derived,
## and absent metadata already means asphalt because asphalt is zero.
const SURFACE_ASPHALT := 0
const SURFACE_CURB := 1
const SURFACE_GRASS := 2

## Names for the report line and the HUD, in `SurfaceType` order.
const SURFACE_NAMES: PackedStringArray = ["asphalt", "curb", "grass", "dirt"]

## How far down the start/finish straight the grid sits, meters.
##
## Not zero. Distance zero is the exit of Turn 4, and a standing start taken from
## inside a 55 m corner is a standing start on camber. 30 m puts the kart on clean
## straight road with the corner behind it and still leaves 190 m to the Turn 1
## kink.
const GRID_DISTANCE := 30.0

## Ride height at spawn, meters, above the road surface.
##
## `proving_ground.gd`'s number and its reason: placing the kart at exactly wheel
## height means the first tick starts with the tires already interpenetrating and
## Jolt resolves that by pushing them apart. A kart that hops on spawn is this, not
## a suspension bug.
const SPAWN_LIFT := 0.12

## Run-off slab thickness, meters.
##
## `proving_ground.gd`'s argument holds here and matters more: a thin slab under a
## fast body invites tunneling, and this scene has a kart arriving at a curb at
## 140 km/h by design. 2 m cannot be passed through in one tick at any speed the
## kart reaches, and it is also the floor under the road ribbon — the road itself
## is a surface, not a volume, so this slab is what catches anything that gets
## past it. 2 mm below, so nothing that goes wrong there is a long fall.
const GROUND_THICKNESS := 2.0

## How far the run-off reaches past the outermost part of the track, meters.
##
## CIK-FIA Circuit Regulations Part 1 §7.5 requires a verge of at least 1.80 m all
## the way round with grass or compacted ground over at least 1 m of it. This is
## 150 m, which is not a safety figure — it is so that a kart that spins off at
## 140 km/h ends up on grass rather than off the edge of the world, and so that
## `--eye` can be parked well outside the track for an overhead still without
## framing the void.
const RUNOFF_MARGIN := 150.0

## Distance boards before the two heavy braking zones, meters before turn-in.
##
## Counted rather than numbered: three bars at 100 m, two at 50 m, one at 25 m.
## `driving_hud.gd` argues the same thing about its shift LEDs — information
## carried by *how many* rather than by hue survives both colorblindness and a
## glance at 140 km/h — and it also means no font, no orientation and no billboard
## on a piece of track furniture.
const BOARD_DISTANCES: PackedFloat32Array = [100.0, 50.0, 25.0]

## Where the kart mesh comes from. Gitignored and regenerated by
## `tools/blender/genkart.sh`, so it is loaded at run time rather than referenced
## from the `.tscn` — a hard `ext_resource` would make the scene fail to load on a
## fresh clone.
const KART_MESH_PATH := "res://assets/generated/kart.glb"

## The names the Blender modules publish their wheel centers under, in
## `CORNER_COUNT` order: FL, FR, RL, RR.
const WHEEL_PIVOT_NAMES: PackedStringArray = [
	"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr",
]

## Ticks to leave the kart's `_physics_process` switched off after a respawn.
## Issue #132; see `_respawn()`.
const RESPAWN_QUIET_TICKS := 1

## The largest steering input at 100 km/h whose geometric radius asks for no more
## lateral g than the kart can actually make: **0.065 of lock, 1.62 degrees**.
##
## Measured by `tests/core/test_vehicle.cpp`'s steering-step case, and duplicated
## here for a HUD row rather than derived, because deriving it needs the tire's peak
## friction and the Ackermann solver and this is a text label. If it drifts, the row
## reports a percentage that is wrong and nothing else breaks — which is why it is
## acceptable here and would not be in `src/core/`.
const FOLLOWABLE_LOCK := 0.065

var _args: Dictionary = {}
var _layout: TrackLayout
var _kart: KartBody
var _chase: ChaseCamera
var _cockpit: CockpitCamera
var _free: FreeCamera
var _fixed: Camera3D
var _hud: Label
var _driving_hud: Control
var _tuning: KartTuning
var _tuning_panel: TuningPanel
## The `EngineVoiceStream` the kart is publishing into, or null where there is no
## audio device. Held so the HUD can read `voice_stats()` — the worst-block
## render figure is the one that sees a dropout, and a mean never will.
var _engine_voice: Object
var _scrub_voice: Object
var _wind_voice: Object

var _physics_draw: PhysicsDraw
var _camera_mode := "chase"
var _spawn := Vector3.ZERO
var _respawn_quiet := 0

## Triangle counts, printed by `_report()`. The road and the curbs are each one
## mesh and one `ConcavePolygonShape3D` built from the same triangles, so these
## are both the render cost and the collision cost, and ARCHITECTURE.md §15 is the
## budget they are read against.
var _road_triangles := 0
var _curb_triangles := 0

## Last tick's surface read-out, so the corner overlay does not ask the boundary
## for a fresh `wheel_report()` on every rendered frame. See `_update_wheel_visuals()`.
var _surface_text := ""

var _wheel_pivots: Array[Node3D] = []
var _wheel_rest_yaw := PackedFloat32Array()


func _ready() -> void:
	_args = Cmdline.parse()

	_layout = TrackLayout.new()
	# A loop that does not close is a hole in the road, and the hole is 2 mm deep
	# and 8 m wide with grass at the bottom of it. `_solve_closure()` is exact
	# arithmetic rather than an iteration, so anything above a millimeter here is
	# a layout that has been edited into a shape the solve cannot reach — see its
	# comment about which two straights may be left free.
	if _layout.closure_error > 1e-3:
		push_error("track layout does not close: %.6f m" % _layout.closure_error)

	_build_environment()
	_build_ground()
	_build_track()
	_build_kart()
	_build_tuning()
	_build_cameras()
	_build_hud()
	# One line, and it is the whole of issue #43's integration: `KartBody::_ready`
	# joins the `telemetry_source` group and the panel finds it there.
	add_child(preload("res://scenes/ui/telemetry.tscn").instantiate())

	_set_camera_mode(Cmdline.as_string(_args, "camera", "chase"))
	_report()


# --- construction ----------------------------------------------------------


func _build_environment() -> void:
	var world_environment := WorldEnvironment.new()
	world_environment.environment = LookEnv.environment(_args)
	world_environment.name = "WorldEnvironment"
	add_child(world_environment)
	add_child(LookEnv.sun(_args))

	# Sized over the **whole world**, not over the driving area, which is the
	# opposite of what `proving_ground.gd` does and was measured rather than
	# chosen. A probe box covering only the layout put a hard rectangular seam
	# across the grass at its own boundary — visible in the first take of
	# `shots/track-04-turn4.png` as two dark corners — because `AMBIENT_DISABLED`
	# stops a probe adding ambient but not specular, and broad rough-surface
	# specular is exactly what a lawn is made of.
	#
	# The cost is resolution: one capture over 730 m resolves nothing at kart
	# scale, which is the failure `kartview.gd` records for the look-dev scene's
	# default box. That trade is right here and would be wrong there — this is a
	# racetrack with no reflective content on it, and a uniformly lit one beats a
	# sharper one with a line drawn across it at 140 km/h.
	var extent := _world_extent()
	var center := _layout.bounds().get_center()
	add_child(LookEnv.reflection_probe(
		Vector3(extent, 80.0, extent),
		Vector3(center.x, 12.0, center.z),
		extent
	))


## The run-off: one slab under everything, `surface_type` 2.
##
## A field with a track-shaped hole in it would be the physically honest shape and
## it is a great deal of geometry for nothing — the road ribbon stands 2 mm above
## this and wins every suspension raycast that lands on it. `track_ribbon.gd`'s
## `ROAD_LIP` carries the argument for the sign and for the size.
##
## 0.18 for grass against asphalt's 1.00 is not a road-car ratio; `surface.h` is
## emphatic about why. In round numbers a kart that holds 1.86 g on the track holds
## 0.33 g here, so the front washes out at walking pace and the rear steps out
## under any throttle at all. Leaving the road is meant to cost the lap.
## How wide the world is, meters: the layout's own extent plus run-off on all
## sides. Square, so the run-off is at least `RUNOFF_MARGIN` everywhere and the
## reflection probe can be the same box.
func _world_extent() -> float:
	var box := _layout.bounds()
	return maxf(box.size.x, box.size.z) + RUNOFF_MARGIN * 2.0


func _build_ground() -> void:
	var box := _layout.bounds()
	var extent := _world_extent()
	var center := box.get_center()

	var grass := Cmdline.as_bool(_args, "grass", true)

	var plane := PlaneMesh.new()
	plane.size = Vector2(extent, extent)
	# Subdivided so vertex-interpolated fog and any eventual lightmap have
	# somewhere to vary. Free at this triangle count.
	plane.subdivide_width = 48
	plane.subdivide_depth = 48

	var visual := MeshInstance3D.new()
	visual.mesh = plane
	if grass:
		var material := TrackRibbon.grass_material()
		material.uv1_scale = Vector3(extent * 0.5, extent * 0.5, 1.0)
		visual.material_override = material
	else:
		visual.material_override = LookEnv.asphalt_material(extent)
	visual.position = Vector3(center.x, 0.0, center.z)
	visual.name = "RunoffVisual"
	add_child(visual)

	var body := StaticBody3D.new()
	body.name = "Runoff"
	# Friction is the *tire model's*, not this material's — `KartBody::_ready` sets
	# its own body's friction to 0.0 and the combine rule is measured to be
	# `min(a, b)` (ADR-0033), so whatever is written here reaches nothing. Left at
	# 1.0 so anything else that ever lands on it behaves normally. The number that
	# matters is the surface type.
	var physics_material := PhysicsMaterial.new()
	physics_material.friction = 1.0
	physics_material.rough = true
	body.physics_material_override = physics_material
	body.set_meta("surface_type", SURFACE_GRASS if grass else SURFACE_ASPHALT)

	var shape := BoxShape3D.new()
	shape.size = Vector3(extent, GROUND_THICKNESS, extent)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	# Sunk so its top face is exactly y = 0, where the visual plane is. A
	# half-thickness error here reads as a kart floating or half-buried, and it is
	# the first thing to check if it does.
	collider.position = Vector3(center.x, -GROUND_THICKNESS * 0.5, center.z)
	body.add_child(collider)
	add_child(body)


func _build_track() -> void:
	_build_road()
	if Cmdline.as_bool(_args, "curbs", true):
		_build_curbs()
	if Cmdline.as_bool(_args, "markings", true):
		_build_markings()


## The asphalt, as one mesh and one collider built from the same triangles.
func _build_road() -> void:
	var ribbon := TrackRibbon.new()
	ribbon.add_road(_layout.samples)

	var visual := MeshInstance3D.new()
	visual.mesh = ribbon.mesh()
	# Extent one, not the track's size. `LookEnv.asphalt_material()` sets
	# `uv1_scale` to `extent / 4.00 m`, and this mesh's UVs are already in meters,
	# so an extent of one leaves exactly the 4 m tile the §5 texel-density standard
	# asks for. Passing the track length here instead would tile the asphalt about
	# two hundred and fifty times too finely.
	visual.material_override = LookEnv.asphalt_material(1.0)
	visual.name = "RoadVisual"
	add_child(visual)

	_road_triangles = ribbon.triangle_count()
	add_child(_static_body("Road", ribbon.faces(), SURFACE_ASPHALT))


## The curbs, `surface_type` 1, at every apex plus two exits.
##
## Where they are and why:
##
##   * **Turn 1, inside.** The fastest thing on the track — a 120 m kink taken at
##     about 140 km/h. #139's acceptance is "a wheel driven at it at 100+ km/h does
##     not pass through it", and this is the only place on the lap that offers that
##     speed against a vertical face.
##   * **Turn 2, inside.** The hairpin apex. A driver fighting #137's scrub will
##     be over this curb whether or not they meant to be.
##   * **Turn 2 exit, outside.** Where a kart that actually rotates ends up. On a
##     kart that pushes straight on instead, it is never touched, which makes it a
##     read-out as much as a curb.
##   * **Turn 3, inside.** The 30 m sweeper that sits on #137's boundary.
##   * **Turn 4, inside, the whole 172 m of it.** A constant-radius corner has no
##     single apex, so the whole inside edge is curbed and the curb becomes the
##     visual reference for the radius — which is the point of the corner.
##   * **Turn 4 exit, outside.** #32's corner. A kart rotating on a lifted inside
##     rear runs out to here; a shopping trolley does not.
##
## `hand` is -1 for the left of the direction of travel and +1 for the right, so
## the inside of a left-hander is -1.
func _build_curbs() -> void:
	var ribbon := TrackRibbon.new()
	var long_end := _layout.segment_end(TrackLayout.SEG_LONG_CORNER)

	ribbon.add_curb(_segment_samples(TrackLayout.SEG_KINK), 1.0)
	ribbon.add_curb(_segment_samples(TrackLayout.SEG_HAIRPIN), -1.0)
	ribbon.add_curb(_layout.samples_between(
		_layout.segment_start(TrackLayout.SEG_POWER_STRAIGHT),
		_layout.segment_start(TrackLayout.SEG_POWER_STRAIGHT) + 40.0), 1.0)
	ribbon.add_curb(_segment_samples(TrackLayout.SEG_SWEEPER), -1.0)
	ribbon.add_curb(_segment_samples(TrackLayout.SEG_LONG_CORNER), -1.0)
	# The Turn 4 exit curb stops at the end of the lap rather than wrapping past
	# the start line onto the grid. Wrapping would need two ranges and would put a
	# curb beside a standing start, which is the one place on the track where
	# nobody wants one.
	ribbon.add_curb(_layout.samples_between(long_end - 45.0, long_end), 1.0)

	var visual := MeshInstance3D.new()
	visual.mesh = ribbon.mesh()
	visual.material_override = TrackRibbon.curb_material()
	visual.name = "CurbVisual"
	add_child(visual)

	_curb_triangles = ribbon.triangle_count()
	add_child(_static_body("Curbs", ribbon.faces(), SURFACE_CURB))


## Paint: the edge lines, the start/finish line, the grid box and the boards.
##
## All one mesh and no collider at all. Paint is not a surface here — a wheel on
## the white line is a wheel on the asphalt, which is both true and the only thing
## that keeps the edge line from being a third grip zone 100 mm wide.
func _build_markings() -> void:
	var ribbon := TrackRibbon.new()
	ribbon.add_edge_lines(_layout.samples)

	var grid := _layout.nearest_sample(_grid_distance())
	ribbon.add_paint_bar(grid, 0.30, -TrackRibbon.TRACK_WIDTH * 0.5, TrackRibbon.TRACK_WIDTH * 0.5)
	# The grid box: a kart is 1.83 m long and about 1.4 m wide, so a 2.4 by 1.8 box
	# is where it stands with a hand's width around it. Two side bars and a back
	# bar, open at the front, the way a real grid box is. Drawn from one sample
	# rather than from twelve, because `nearest_sample()` snaps to the 2 m straight
	# spacing and twelve short bars would all land on the same three samples.
	# Centered on the grid rather than behind it: `chassis.h` puts the chassis
	# origin near the middle of the kart, so a box drawn back from the spawn leaves
	# the kart's nose sticking out of it.
	var box := _layout.nearest_sample(_grid_distance())
	ribbon.add_paint_bar(box, 2.4, -0.975, -0.825)
	ribbon.add_paint_bar(box, 2.4, 0.825, 0.975)
	ribbon.add_paint_bar(_layout.nearest_sample(_grid_distance() - 1.2), 0.15, -0.9, 0.9)

	# Distance boards, on the outside of each of the two braking zones. Both
	# corners turn left, so the outside is +1.
	_add_boards(ribbon, _layout.segment_start(TrackLayout.SEG_HAIRPIN), 1.0)
	_add_boards(ribbon, _layout.segment_start(TrackLayout.SEG_SWEEPER), 1.0)

	var visual := MeshInstance3D.new()
	visual.mesh = ribbon.mesh()
	var paint := TrackRibbon.paint_material()
	# Paint sits on asphalt, not in it. Without this the lines z-fight the road
	# along the whole lap, which looks like a rendering bug and is one.
	paint.render_priority = 1
	visual.material_override = paint
	visual.name = "MarkingVisual"
	add_child(visual)


func _add_boards(ribbon: TrackRibbon, turn_in: float, hand: float) -> void:
	var half := TrackRibbon.TRACK_WIDTH * 0.5
	for index in BOARD_DISTANCES.size():
		var bars := BOARD_DISTANCES.size() - index  # 3 at 100 m, 2 at 50, 1 at 25
		var sample := _layout.nearest_sample(turn_in - BOARD_DISTANCES[index])
		for bar in bars:
			var offset := hand * (half + 1.6 + float(bar) * 0.7)
			ribbon.add_paint_bar(sample, 3.0, offset - 0.2, offset + 0.2)


## One `StaticBody3D` carrying one piece of surface metadata over one collider.
##
## `backface_collision` is on. The winding is right — `track_ribbon.gd` measured
## Godot's convention rather than recalling it — but a road built as a *surface*
## rather than as a volume has a failure mode a road slab does not: anything that
## ends up underneath it falls through the world. Two-sided triangles turn that
## into a body being pushed back out, and the cost is that a ray fired from below
## finds the road, which nothing in this project does.
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


func _segment_samples(segment: int) -> Array[Dictionary]:
	return _layout.samples_between(_layout.segment_start(segment), _layout.segment_end(segment))


func _grid_distance() -> float:
	return Cmdline.as_float(_args, "start", GRID_DISTANCE)


# --- the kart, which is `proving_ground.gd`'s asset plumbing ----------------


func _build_kart() -> void:
	var grid := _layout.nearest_sample(_grid_distance())
	var heading: float = grid["heading"]
	_spawn = (grid["position"] as Vector3) + Vector3(0.0, TrackRibbon.ROAD_LIP + SPAWN_LIFT, 0.0)
	# **The sign here is the thing that is invisible until the kart drives
	# backwards.** `TrackLayout.forward()` is `(sin h, 0, -cos h)`, so heading zero
	# is -Z, which is Godot's forward and the direction `chassis.h` builds toward.
	# A `Basis` rotated by +h about Y carries -Z to `(-sin h, 0, -cos h)`, which is
	# the mirror of that, so the rotation the kart wants is **-h**. At the default
	# grid, heading is zero and this is the identity, which is exactly why it would
	# have gone unnoticed if it were wrong.
	var basis := Basis(Vector3.UP, -heading)

	_kart = KartBody.new()
	_kart.name = "Kart"
	# Set before `add_child`, because `KartBody::_ready` captures its spawn from
	# whatever transform it finds if nobody has told it one.
	_kart.transform = Transform3D(basis, _spawn)
	_kart.set_spawn(Transform3D(basis, _spawn))

	var visual := _load_mesh(_kart)
	if visual != null:
		var pivots := _wheel_pivots_of(visual)
		if pivots.size() == WHEEL_PIVOT_NAMES.size():
			_wheel_pivots = pivots
			for pivot in pivots:
				_wheel_rest_yaw.append(pivot.rotation.y)
			_build_collision(_kart, visual, pivots)
		else:
			push_error(
				"kart mesh has %d of the %d wheel pivots (wheel_fl/fr/rl/rr) — "
				% [pivots.size(), WHEEL_PIVOT_NAMES.size()]
				+ "run tools/blender/genkart.sh"
			)
	add_child(_kart)

	# The engine note, at the engine. Null under every headless gate, which have no
	# audio device and must not need one — see `engine_voice_rig.gd`.
	_engine_voice = EngineVoiceRig.attach(_kart)
	# Tire scrub and wind. Issue #84. Three emitters, because the three are not in
	# the same place: the note at the engine mount, the scrub at the rear axle, and
	# the wind at the driver's head and not in the world at all.
	var noise: Array = EngineVoiceRig.attach_noise(_kart)
	_scrub_voice = noise[0]
	_wind_voice = noise[1]
	# The listener, at the driver's head. Issue #160: without it Godot falls back to
	# whichever `Camera3D` is current, which was measured to swing the level 20.7 dB
	# as the chase camera moves — so the mix changed every time somebody pressed V,
	# and only one of the two mixes was ever judged.
	EngineVoiceRig.attach_listener(_kart)

	# `--auto-shift=off` starts in the manual box, so a session that is about the
	# gearbox does not begin by reaching for a key. Defaults to the assist's own
	# default rather than to a literal, so this flag cannot become a second owner
	# of what "on" means.
	_kart.auto_shift = Cmdline.as_bool(_args, "auto-shift", _kart.auto_shift)

	# So a session can start at a chosen curve instead of dialing back to it, and so
	# that whatever value the brackets settle on is reproducible from a command.
	_kart.steer_gamma = Cmdline.as_float(_args, "steer-gamma", _kart.steer_gamma)

	_physics_draw = PhysicsDraw.new()
	_physics_draw.name = "PhysicsDraw"
	add_child(_physics_draw)
	_physics_draw.set_target(_kart)

	# Scripted input, for stills and for anything with no keyboard. Held constant
	# for the whole run, so the same command produces the same still.
	if _args.has("throttle") or _args.has("steer") or _args.has("brake"):
		var throttle := Cmdline.as_float(_args, "throttle", 0.0)
		var brake := Cmdline.as_float(_args, "brake", 0.0)
		var steer := Cmdline.as_float(_args, "steer", 0.0)
		_kart.input_driver = func() -> Dictionary:
			return {"throttle": throttle, "brake": brake, "steer": steer}


## The generated kart, parented under the body. `proving_ground.gd`'s, unchanged.
func _load_mesh(body: Node3D) -> Node3D:
	if not ResourceLoader.exists(KART_MESH_PATH):
		push_error("no kart at %s — run tools/blender/genkart.sh" % KART_MESH_PATH)
		return null
	var instance := (load(KART_MESH_PATH) as PackedScene).instantiate() as Node3D
	instance.name = "KartMesh"
	body.add_child(instance)
	return instance


func _wheel_pivots_of(root: Node3D) -> Array[Node3D]:
	var found: Array[Node3D] = []
	for wheel_name in WHEEL_PIVOT_NAMES:
		var node := root.find_child(wheel_name, true, false) as Node3D
		if node != null:
			found.append(node)
	return found


## A single box for the chassis, sized from the mesh minus the wheels.
##
## `proving_ground.gd`'s, unchanged, including the two reasons the wheels are
## excluded: a collider over a tire fights its own suspension ray, and a box that
## reached the road would have Jolt supplying a second normal force on top of the
## suspension's.
func _build_collision(body: Node3D, visual: Node3D, pivots: Array[Node3D]) -> void:
	var bounds := _visual_bounds(visual, pivots)
	var shape := BoxShape3D.new()
	# The underside sits at the bottom of the frame rails rather than at the lowest
	# thing on the kart, so a curb strike catches the frame and not an invisible
	# skirt. `track_ribbon.gd`'s `CURB_HEIGHT` is bracketed against this number.
	var floor_y := 0.035
	var top := bounds.position.y + bounds.size.y
	shape.size = Vector3(bounds.size.x, maxf(top - floor_y, 0.05), bounds.size.z)

	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.position = Vector3(
		bounds.get_center().x, floor_y + shape.size.y * 0.5, bounds.get_center().z
	)
	collider.name = "ChassisShape"
	body.add_child(collider)


func _visual_bounds(root: Node3D, excluded: Array[Node3D]) -> AABB:
	var bounds := AABB()
	var started := false
	for node in _descendants(root):
		var visual := node as VisualInstance3D
		if visual == null or visual.get_aabb().size == Vector3.ZERO:
			continue
		var skip := false
		for pivot in excluded:
			if visual.is_ancestor_of(pivot) or pivot.is_ancestor_of(visual):
				skip = true
				break
		if skip:
			continue
		var world := (root.transform * _relative_transform(root, visual)) * visual.get_aabb()
		if not started:
			bounds = world
			started = true
		else:
			bounds = bounds.merge(world)
	return bounds


func _relative_transform(root: Node3D, node: Node3D) -> Transform3D:
	var accumulated := Transform3D()
	var walker := node
	while walker != null and walker != root:
		accumulated = walker.transform * accumulated
		walker = walker.get_parent() as Node3D
	return accumulated


func _descendants(node: Node) -> Array[Node]:
	var found: Array[Node] = []
	for child in node.get_children():
		found.append(child)
		found.append_array(_descendants(child))
	return found


# --- cameras and overlays --------------------------------------------------


func _build_cameras() -> void:
	_chase = ChaseCamera.new()
	_chase.name = "ChaseRig"
	add_child(_chase)
	_chase.set_target(_kart)
	_chase.camera.attributes = LookEnv.camera_attributes(_args)

	# The cockpit rig, parented to the kart rather than to the scene: the eye is a
	# position in the body frame and a rig in the scene would have to re-derive the
	# kart's transform every frame to put it there. Not a `SpringArm3D` and it wants
	# no wall raycast — the eye is inside the kart, so there is nothing between it
	# and what it is looking at.
	_cockpit = CockpitCamera.new()
	_cockpit.name = "CockpitRig"
	_kart.add_child(_cockpit)
	_cockpit.set_target(_kart)
	_cockpit.camera.attributes = LookEnv.camera_attributes(_args)
	# Further than the proving ground's default, for the same reason `_fixed` is:
	# from T1 the far side of a 1,030 m loop is most of a kilometer away.
	_cockpit.camera.far = 2000.0

	# The rig is a `SpringArm3D` and it is deliberately *not* told to ignore the
	# track. It collides with the world on purpose — that is the fourth item of
	# ARCHITECTURE.md §7's chase spec, free because `SpringArm3D` is the wall
	# raycast — and the arm sits 1.05 m up and 3.4 m back, which clears a 30 mm curb
	# by two orders of magnitude. Excluding the road would trade a snag that cannot
	# happen for a camera that can end up under it.

	_free = FreeCamera.new()
	_free.name = "FreeCamera"
	_free.attributes = LookEnv.camera_attributes(_args)
	add_child(_free)

	# `--eye=x,y,z --look=x,y,z` parks a camera and leaves it there, because a
	# chase rig frames whatever the kart is doing and two runs put it in two
	# places. An image is described by the command that made it.
	var eye := Cmdline.as_string(_args, "eye", "")
	if eye == "":
		return
	_fixed = Camera3D.new()
	_fixed.name = "FixedCamera"
	_fixed.fov = Cmdline.as_float(_args, "fov", 55.0)
	_fixed.near = 0.05
	# Further than the proving ground's 900 m: an overhead still of a 433 m-long
	# layout is taken from about 400 m up and the far corner is further away again.
	_fixed.far = 2000.0
	_fixed.attributes = LookEnv.camera_attributes(_args)
	add_child(_fixed)
	var target := _parse_point(Cmdline.as_string(_args, "look", ""), _spawn)
	var eye_point := _parse_point(eye, _spawn + Vector3(8.0, 3.0, 26.0))
	# A 1,030 m loop wants an overhead still, and an overhead still points the
	# camera straight down, where `Vector3.UP` is colinear with the view direction.
	# Godot warns and then picks a roll of its own, so two runs of the same command
	# frame the track two ways — which breaks the one rule every still in this
	# project is held to. -Z instead, so the frame is oriented the way the kart
	# leaves the grid.
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


## The tuning registry and its overlay. ROADMAP M3b's last bullet, ADR-0037.
##
## **This replaces the `[` / `]` prototype, and the prototype was the wrong shape
## rather than merely small.** It moved one constant with two keys, wrote nothing
## down about what it had moved, and — the part that matters — `steer_gamma` could
## be a long way from 3.0 while every instrument in the scene still reported a kart
## at its defaults. A session that found a good number left a line in a terminal
## scrollback and nothing else.
##
## What replaces it answers the same question and three more: what is this number,
## where does it live, where did it come from, and what have I moved away from its
## source. `steer_gamma` is now one row of fourteen and is reached the same way as
## every other one.
func _build_tuning() -> void:
	if _kart == null:
		return
	_tuning = KartTuning.new()
	_tuning.name = "Tuning"
	add_child(_tuning)
	# After `add_child`, because the path is resolved relative to this node and the
	# setter re-applies immediately once it is in the tree.
	_tuning.set_vehicle_path(_tuning.get_path_to(_kart))

	# The audio half. Null under every headless gate, where `EngineVoiceRig.attach`
	# returns nothing because there is no audio device — so the thirteen audio
	# tunables simply have nobody listening, which is correct rather than broken.
	var voice_player := _kart.get_node_or_null("EngineVoice") as AudioStreamPlayer3D
	if voice_player != null:
		EngineVoiceRig.bind_tuning(_tuning, _engine_voice, voice_player,
				_scrub_voice, _wind_voice)

	# `--steer-gamma=` still works and now goes through the registry, so a session
	# started at a chosen curve reports itself as tuned instead of claiming to be at
	# defaults. Any tunable can be set the same way: `--tune=key=value`, repeatable.
	#
	# The old code wrote `_kart.steer_gamma` directly. That was a second owner of the
	# constant, and it is exactly the failure the audit exists to catch — the kart
	# would have been tuned and `is_at_defaults()` would have said otherwise.
	#
	# Comma separated rather than a repeated flag, because `Cmdline` parses into a
	# Dictionary and a second `--tune=` would silently replace the first.
	if _args.has("steer-gamma"):
		_apply_tuning_argument("steer_gamma", Cmdline.as_float(_args, "steer-gamma", 3.0))
	var assignments := Cmdline.as_string(_args, "tune", "")
	if assignments != "":
		for assignment in assignments.split(",", false):
			var pair := assignment.split("=", true, 1)
			if pair.size() != 2:
				push_error("--tune wants key=value[,key=value], got '%s'" % assignment)
				continue
			_apply_tuning_argument(pair[0].strip_edges(), float(pair[1]))

	# And a whole preset, so a session can start from where the last one finished.
	var preset := Cmdline.as_string(_args, "preset", "")
	if preset != "":
		var outcome: Dictionary = _tuning.load_preset(preset)
		for warning in outcome.get("warnings", PackedStringArray()):
			push_warning("preset: %s" % warning)
		if not bool(outcome.get("ok", false)):
			push_error("preset %s did not load" % preset)


## One `--tune` assignment, with the acknowledgement handled the way the file
## format handles it: a command line is written down and reproducible, so it is an
## explicit override in the same sense a `!` line in a preset is. It is still
## reported, loudly, by `acknowledge` itself.
func _apply_tuning_argument(key: String, value: float) -> void:
	var id: int = _tuning.id_of(key)
	if id < 0:
		push_error("no tunable named '%s' — run tools/verify/tuning.sh to list them" % key)
		return
	if _tuning.is_defended(id):
		_tuning.acknowledge(id)
	_tuning.set_value(id, value)


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
	# not one, because they answer different questions — its own header argues that
	# at length, and it disables itself under a headless display server so a
	# scenario report is not buried under forty draw errors a tick.
	#
	# `bind()` rather than an exported property: it reads the limiter and rollover
	# thresholds off the body once, and `_build_kart()` has already run.
	_driving_hud = preload("res://scripts/ui/driving_hud.gd").new()
	_driving_hud.name = "DrivingHud"
	layer.add_child(_driving_hud)
	if _kart != null:
		_driving_hud.bind(_kart)

	# The tuning overlay, on F2. It finds the registry through the `tuning_source`
	# group the same way the telemetry panel finds the kart, so this is the whole
	# integration and neither node learns the other exists.
	if _tuning != null:
		_tuning_panel = TuningPanel.attach(self)


## What the track is, printed once, so a driving session starts with the numbers
## the corners were sized from rather than with a shape on a screen.
func _report() -> void:
	# `%.9f` rather than an exponent, because GDScript's `%` format has no `%e` and
	# silently leaves the literal in the string rather than erroring — which it did
	# here, on the first run of this file.
	print("test track: %.1f m lap, closes to %.9f m, %d centerline samples" % [
		_layout.length(), _layout.closure_error, _layout.samples.size(),
	])
	print("  T1 kink     R %5.1f m  %+6.1f deg   flat, ~1.28 g at 140 km/h" % [
		TrackLayout.KINK_RADIUS, TrackLayout.KINK_ANGLE])
	print("  T2 hairpin  R %5.1f m  %+6.1f deg   past quarter lock — issue #137" % [
		TrackLayout.HAIRPIN_RADIUS, TrackLayout.HAIRPIN_ANGLE])
	print("  T3 sweeper  R %5.1f m  %+6.1f deg   1.59 g / 81 km/h at quarter lock" % [
		TrackLayout.SWEEPER_RADIUS, TrackLayout.SWEEPER_ANGLE])
	print("  T4 long     R %5.1f m  %+6.1f deg   1.82-1.84 g sustained — issue #32" % [
		TrackLayout.LONG_RADIUS, TrackLayout.LONG_ANGLE])
	print("  straights   %.1f start / %.2f braking / %.1f power / %.2f approach m" % [
		TrackLayout.START_STRAIGHT, _layout.braking_straight,
		TrackLayout.POWER_STRAIGHT, _layout.approach_straight,
	])
	print("  geometry    %d road triangles + %d curb, drawn and collided from the same array" % [
		_road_triangles, _curb_triangles,
	])
	if Cmdline.as_bool(_args, "validate", false):
		# ARCHITECTURE.md §11's M5 validation item, pulled forward: "closed loop, no
		# self-intersection". O(n^2) over 900 samples, so it is opt-in rather than
		# paid on every launch.
		var separation := _layout.min_separation()
		var needed := TrackRibbon.TRACK_WIDTH + TrackRibbon.CURB_WIDTH * 2.0
		print("  min separation %.2f m between parts of the lap over 40 m apart (needs %.2f)" % [
			separation, needed])
		if separation < needed:
			push_error("track self-intersects: %.2f m apart, needs %.2f" % [separation, needed])


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
	if _kart != null and Input.is_action_just_pressed(&"auto_shift_toggle"):
		_kart.auto_shift = not _kart.auto_shift
	# `look_back` has been bound in `project.godot` and printed in this HUD since
	# M3a, and until now **nothing read it** — C and Triangle did nothing at all.
	# That is the exact failure CLAUDE.md's driving section opens with, one level
	# worse: the key was advertised rather than omitted. The chase rig's own
	# look-back is separate work and has its own issue; this is the cockpit's, where
	# looking back is a head turn and is three lines.
	if _cockpit != null:
		_cockpit.set_looking_back(Input.is_action_pressed(&"look_back"))
	# Respawn is the scene's, not the kart's: `respawn()` moves a body to a pose
	# only whoever placed it knows, so it is a method someone calls rather than an
	# action a node listens for. Without this line R and the pad's Circle do
	# nothing, and the first time the kart ends up on its roof the session is over —
	# which on a track with a hairpin built to stop the kart is the first minute.
	if _kart != null and Input.is_action_just_pressed(&"respawn"):
		_respawn()
	if _hud != null:
		_hud.text = _hud_text()


func _physics_process(_delta: float) -> void:
	# Issue #132's quiet tick, counted in physics steps rather than in rendered
	# frames. See `_respawn()`.
	if _respawn_quiet > 0:
		_respawn_quiet -= 1
		if _respawn_quiet == 0:
			_kart.set_physics_process(true)
	_update_wheel_visuals()


## Back to the grid, with one force-free tick after it, and with the driving HUD's
## peaks cleared.
##
## Issue #132 is the quiet tick: applying a force at an offset on the same tick a
## body is teleported resolves the lever arm against a center-of-mass position the
## teleport has not reached yet, and the body picks up about 2.1e-3 rad/s of spin
## that nothing asked for.
##
## The `reset_peaks()` is the other half and it matters more here than on the
## proving ground. `driving_hud.gd` holds PEAK and SUST for the whole session, and
## on a track with a hairpin in it a driver spins, respawns, and then spends the
## next twenty minutes reading a peak lateral figure that belongs to the spin. A
## number that survives the event it describes is a lie, and #32 and #40 are both
## judged off that panel.
func _respawn() -> void:
	_kart.respawn()
	_kart.set_physics_process(false)
	_respawn_quiet = RESPAWN_QUIET_TICKS + 1
	if _driving_hud != null:
		_driving_hud.reset_peaks()


## Turn the front wheel meshes to the angle the solver is steering at.
##
## The angle comes out of `wheel_report()` per corner rather than from the steering
## input, so what is drawn is `steering.h`'s Ackermann output — the two front
## wheels are at *different* angles in a corner, and that is visible. Wheel spin is
## not animated, because nothing publishes a per-wheel angle.
func _update_wheel_visuals() -> void:
	if _kart == null:
		return
	var report: Array = _kart.wheel_report()
	# Cached here rather than asked for again in `_process`. `wheel_report()`
	# allocates an Array of four Dictionaries, and `driving_hud.gd`'s header makes
	# the argument in its own words: an allocation is fine for a panel that samples
	# while it is open and wrong for an overlay that draws every frame. This tick
	# already has the report, so the overlay reads a String instead.
	_surface_text = surface_line(report)
	for index in mini(_wheel_pivots.size(), report.size()):
		var wheel: Dictionary = report[index]
		_wheel_pivots[index].rotation.y = _wheel_rest_yaw[index] + float(wheel["steer_angle"])


func _set_camera_mode(mode: String) -> void:
	# A parked camera outranks the mode argument: cycling away from it would break
	# the still it was asked for.
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
		# The kart keeps simulating but stops reading input, because the flight
		# camera reuses the drive keys.
		_kart.set_process_input_enabled(false)
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
		return
	if _camera_mode == "cockpit":
		_cockpit.make_current()
	else:
		_chase.camera.current = true
	_kart.set_process_input_enabled(true)
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


## The corner overlay. `proving_ground.gd`'s four lines plus one this track needs.
##
## The extra line is what each wheel is standing on. On a flat plane that question
## has one answer forever; here it is the only way to tell, at the wheel, whether a
## corner was lost on the curb, on the grass or on the asphalt — and it is read
## straight off `wheel_report()`'s `surface`, which is `KartBody::query_ground`'s
## own answer rather than a second guess at it.
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
		rad_to_deg(_kart.steer_input * _kart.get_steer_lock()),
		_kart.get_lateral_g(), _kart.get_longitudinal_g(),
	])
	lines.append("under the wheels  " + _surface_text)
	# The steering curve stays on this line even though the tuning overlay now owns
	# every knob, because it is the one tunable whose *consequence* is a sentence
	# rather than a number. `FOLLOWABLE_LOCK` is the measured 100 km/h limit — see
	# `tests/core/test_vehicle.cpp` — so this reports what fraction of stick travel
	# reaches it at the current gamma. At gamma 1 it is 6.5%, which is inside the
	# 0.15 deadzone and is the whole reason ADR-0036 exists.
	var gamma: float = _kart.steer_gamma
	var tuned := ""
	if _tuning != null and not _tuning.is_at_defaults():
		tuned = "   TUNED: %d changed, %d defended" % [
			_tuning.changed_count(), _tuning.defended_override_count(),
		]
	lines.append("steer curve  gamma %.2f   the 100 km/h limit sits at %.0f%% of stick%s"
		% [gamma, 100.0 * pow(FOLLOWABLE_LOCK, 1.0 / gamma), tuned])
	lines.append("physics %d Hz   frame %.1f fps   %s" % [
		Engine.physics_ticks_per_second,
		Engine.get_frames_per_second(),
		ProjectSettings.get_setting("physics/3d/physics_engine", "?"),
	])
	# `include_debug` off, because this scene has F2's tuning overlay on the end of
	# the same line and the shared list does not know about it.
	lines.append_array(ControlHints.lines(false))
	lines.append("F2 tuning  F3 telemetry  F4 frustum  F5 physics")
	return "\n".join(lines)


## "FL asphalt  FR curb  RL asphalt  RR curb", from the solver's own ground query.
##
## Takes a `wheel_report()` rather than a kart, so the caller decides when that
## allocation happens; static so anything else can format the same line.
static func surface_line(report: Array) -> String:
	var parts: Array[String] = []
	for wheel: Dictionary in report:
		var surface := int(wheel["surface"])
		var label: String = SURFACE_NAMES[surface] if surface < SURFACE_NAMES.size() else "?"
		if not bool(wheel["contact"]):
			label = "-air-"
		parts.append("%s %s" % [String(wheel["name"]).to_upper(), label])
	return "  ".join(parts)
