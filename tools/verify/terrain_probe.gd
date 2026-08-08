extends SceneTree

## Drive the kart on the **real circuit**: real collision triangles, real contact
## normals, real per-wheel `surface_grip`. Issue #240.
##
##     tools/verify/terrain.sh
##     godot --headless --path . --script tools/verify/terrain_probe.gd -- \
##         --case=curb
##
## ## Why this file exists
##
## Every other numeric driving rig in this repo drives a plane.
## `tests/core/test_vehicle.cpp`'s `Rig` synthesizes a `GroundQuery` with
## `normal = (0, 1, 0)` and `surface_grip = 1.0`; `test_scrub_energy.cpp` and
## `test_yaw_stability.cpp` each carry their own copy of it; `drive.sh` drives the
## proving ground, which `ARCHITECTURE.md` describes as having "no features on
## purpose". So every §6.4 figure, every #137 measurement and ADR-0071 and
## ADR-0072 describe a kart **on a plane**, and the first person to drive the build
## hit something in thirty seconds that all of them call impossible.
##
## The plane is the *correct* fixture for the question each of those files asks —
## isolating a tire law needs the ground held constant. The defect is that no
## second fixture was ever built. This is the second fixture.
##
## ## What it does NOT do
##
## It does not tune anything, and it holds no constant of its own that could move a
## published figure. Every input is an open-loop function of the tick counter, in
## `drive_probe.gd`'s words and for `drive_probe.gd`'s reason: a closed-loop
## scenario measures the controller as much as the kart, and two runs of a
## controller reading its own noisy state are not guaranteed to agree.
##
## ## The four things that make this a measurement rather than a demo
##
##   1. **The ground is built from `KartTrack.surface_meshes()`**, one
##      `StaticBody3D` per surface type carrying `surface_type` metadata, exactly as
##      `circuit.gd` builds it — that metadata is the only path by which
##      `KartBody::query_ground` learns a wheel is on a kerb. Plus `TrackTerrain`'s
##      height field and `circuit.gd`'s base slab, because "what is under the kart
##      when it leaves the road" is half the question.
##   2. **The kart is placed by raycasting into that collision**, not by evaluating
##      the cross-section formula a second time. `--case=place` then compares the
##      raycast placement against `KartTrack.grid_transform()`, which *is* the
##      formula: two independent answers that have to agree, rather than one answer
##      trusted twice.
##   3. **The scripted input is asserted to be the input the solver consumed.** A
##      valid `KartBody.input_driver` Callable overrides the pushed input (ADR-0040)
##      and getting that ordering backwards once made all four `drive.sh` scenarios
##      agree on one hash while measuring nothing. Every tick compares the commanded
##      throttle and brake against `KartBody.throttle_input` / `brake_input`, and a
##      commanded lock has to show up in `steer_input`. `--break=input` is the
##      negative control for exactly that.
##   4. **`--case=calibrate` comes first.** The same rig, on a synthetic plane built
##      to `proving_ground.gd`'s own numbers, has to reproduce `drive.sh`'s figures.
##      Until it does, every other number here is measuring the rig.
##
## ## Ground selection
##
## `--ground=circuit` (default) or `--ground=flat`, and it is a **process-level**
## choice rather than a per-run one. Both worlds in one scene would have to be
## separated in XZ, and at the tens of kilometers that would take, `real_t` is
## `float`: the quantum at 20 km is 2.4 mm, which is larger than the suspension
## travel this rig reports in millimeters. So the flat half of a comparison is a
## second process, and `terrain.sh` runs it.

const TRACK_PATH := "res://data/tracks/valdirone_nuova.track.json"

## The proving ground's own ground, copied number for number from
## `proving_ground.gd` so that `--case=calibrate` is comparing the rig and not two
## different planes. 2,400 m square, 2 m thick, top face at exactly y = 0.
const FLAT_SIZE := 2400.0
const FLAT_THICKNESS := 2.0

## `proving_ground.gd`'s spawn: the near end of the straight, facing -Z, well back
## from the edge for the same reason the plane is large.
const FLAT_SPAWN := Vector3(0.0, 0.12, 1000.0)

## `circuit.gd`'s, for the base slab under the height field.
const GROUND_THICKNESS := 2.0
const RUNOFF_MARGIN := 60.0

## Ticks ignored at the start of every run. `drive_probe.gd`'s number and its
## reason: the kart is spawned above the ground and dropped, so the first fraction
## of a second is a landing rather than a measurement.
const SETTLE_TICKS := 90

## The settle the ground cases use instead, ticks.
##
## **Derived, and it is not a shortcut.** What `SETTLE_TICKS` exists for is the
## spawn drop: `KartRig.SPAWN_LIFT` is 0.12 m and a free fall of 0.12 m takes
## `sqrt(2h/g)` = **0.156 s**, so 30 ticks is 0.25 s, which is 1.6 free-fall times
## plus the suspension's own settle. `drive_probe.gd`'s 90 is generous rather than
## necessary and it is kept unchanged for `--case=calibrate` and `--case=sixfour`,
## because those two are compared against `drive.sh` and the window a figure was
## averaged over is part of the figure.
##
## The ground cases cannot use 90. At 129 km/h a kart running straight in T2's 42 m
## arc crosses the 1 m kerb strip **between t = 0.19 s and t = 0.32 s** — entirely
## inside a 0.75 s settle. The first cut of `--case=curb` reported *zero* kerb
## wheel-ticks for the run-wide row and 82.4% grass, which reads exactly like a
## kerb collider that is not there and was a measurement window that opened after
## the kerb had gone past.
const GROUND_SETTLE_TICKS := 30

## How often the state hash absorbs a sample. `drive_probe.gd`'s.
const HASH_INTERVAL := 4

## Body slip past which a run is a spin rather than a corner, radians.
##
## **Not chosen here.** It is `steady_corner`'s cutoff in
## `tests/core/test_vehicle.cpp` (0.52 rad), `tire_probe.gd`'s `DEPARTURE_RAD` and
## `drive_probe.gd`'s, reused unchanged so that all four mean the same thing by
## "no longer following the road".
const DEPARTURE_RAD := deg_to_rad(30.0)

## Speed below which a longitudinal or lateral spike is not published as a peak.
## `drive_probe.gd`'s, same reason: at walking pace one tick of full brake is a
## legitimate 12 g and that number describes the timestep.
const PEAK_SPEED_GATE := 5.0

## Lateral g above which a row is reporting an impact rather than a tire.
##
## **Derived from the vehicle, not picked.** `KartBody.get_rollover_threshold_g`
## puts this kart's quasi-static rollover at 2.43 g turning left and 2.81 g turning
## right (`src/core/chassis.h`), and `kz_reference.h`'s transient ceiling is 2.5 g.
## Nothing the tires can do reaches 5 g; the run-off rows here reach 100 to 140.
const COLLISION_G := 5.0

## Ride height at spawn. `KartRig.SPAWN_LIFT`, restated only because the placement
## here lifts along the **road normal** rather than along world up, and on 8%
## crossfall those are not the same displacement.
const SPAWN_LIFT := 0.12

## How far above the sampled centerline elevation a placement ray starts, and how
## far it reaches. 10 m of headroom clears the tallest kerb and every barrier; 30 m
## of reach finds the base slab from any point on a circuit that climbs 12.55 m.
const PLACE_RAY_LIFT := 10.0
const PLACE_RAY_LENGTH := 30.0

## Tolerance on `--case=calibrate`, as a fraction of the reference figure.
##
## **Bracketed by two measurements rather than chosen.** At `TICK_OFFSET` the rig
## reproduces `drive.sh` to 0.001% on top speed and 0.076% on the 0-100, and the
## 0.076% is 0.003 s — below the precision `drive_probe.gd` prints. `--break=calib`
## puts the same two figures 0.587% and 1.218% out. So the tolerance has to sit
## above 0.076% and below 0.587%, and 0.2% is between them with 2.6x of margin
## below and 2.9x above. A larger number would be a gate the sabotage could walk
## through; a smaller one would be a gate that fails on the reference's own
## rounding.
##
## What it does **not** buy is bit-identity: the state hashes differ from
## `drive.sh`'s, and they have to — this rig builds one kart per run inside the
## physics loop and `proving_ground.tscn` builds a whole scene with a skidpad
## circle and a grass patch in it. Two figures agreeing to their printed precision
## is the claim, and it is the claim `_check_calibrate` makes.
const CALIBRATE_TOLERANCE := 0.002

## What the run's tick counter reads on the frame after the kart was built.
##
## **Measured, and it is the whole of the residual against `drive.sh`.** This rig
## builds its kart *inside* `_physics_process`, because it runs several scenarios
## in one process; `drive_probe.gd` instantiates a whole scene in `_initialize`, so
## its kart is in the tree before frame zero. `SceneTree` calls the main loop's
## `_physics_process` **before** it propagates the notification to nodes, so on the
## frame a kart is created here the body steps once with nobody watching, and the
## counter and the body then disagree by that step for the whole run.
##
## Swept on `--case=calibrate --ground=flat` against `drive.sh`'s 140.1 km/h and
## 4.38 s, one run per row:
##
##     tick-offset   top km/h   err     0-100 s   err
##          0         140.14   0.026%    4.367   0.304%
##          1         140.12   0.013%    4.375   0.114%
##          2         140.10   0.001%    4.383   0.076%
##          3         140.08   0.015%    4.392   0.266%
##
## 2 is where both columns bottom out, and it is not a fudge: the body has taken
## one step the counter did not see, and `drive_probe.gd` samples once before its
## body has stepped at all, so the two harnesses are two steps apart by
## construction. The residual 0.076% is 0.003 s, which is **smaller than the
## precision `drive_probe.gd` prints its own figure at** (`%.2f`), so the agreement
## is as exact as the reference can express. Top speed moves by 0.02 km/h a step,
## which is the sign that this is an offset in the clock and not a difference in
## the vehicle.
const TICK_OFFSET := 2

## Where the §6.4 accel and brake scenarios are run on the circuit.
##
## Station 1310 in the forward layout, which is **derived and not picked**: it is
## on `Rettifilo del Banco`, the circuit's longest straight (165.0 m, measured by
## `KartTrack.measurements()`), it is past T8's exit kerb, and `--case=survey`
## reports curvature 0, bank 0.00%, grade +0.787% and crown 2.00% there. It is the
## flattest, straightest asphalt the circuit has.
##
## **It is still not flat, and no station on this circuit is.** `--case=survey`
## counts the stations that are straight, ungraded, unbanked and uncrowned all at
## once — the surface `tests/core/`'s three rigs synthesize — and there are none.
## The crown is the term that gets forgotten: 2% of cross slope on every meter of
## road, and a kart on the centerline straddles the ridge with its left wheels on
## -2% and its right wheels on +2%.
##
## **The straight is not long enough for the accel scenario, and that is a result
## rather than a limitation of this file.** `drive.sh` needs 256 m to reach terminal
## velocity and Valdirone's longest straight is 165.0 m. See the `on road` column.
const SIXFOUR_STATION := 1310.0

## The stations the named cases run at, each derived from a measurement rather than
## chosen, and each printed with its own geometry so the choice is checkable.
##
##   crossfall_max   834.0   `--case=survey`'s max |bank|: 8.00% at T5 Vigna. It
##                           is inside T5's arc, because crossfall exists to bank
##                           a corner — the circuit's worst crossfall on a
##                           *straight* is 3.65%, and that is reported too.
##   crossfall_neg   550.0   -7.00% at T3 Il Pozzo, and the circuit's low point
##   grade_down      430.0   -4.60%, straight, unbanked, 97 m of clear road
##                           before T3's braking zone: the cleanest isolation of a
##                           grade the circuit offers
##   grade_up        600.0   +4.60%, straight, unbanked, 125 m clear to T4
##   curb_t2         340.0   4 m before the T2 Lama kerb, which `track.json`
##                           calls "the reference kerb", right side, 30 mm,
##                           spanning 344.306-368.306 m
##   verge           250.0   straight, unbanked, 11.0 m wide: the cheapest place
##                           to put two wheels over the edge at speed
const CASE_STATIONS := {
	"crossfall_max": 834.0,
	"crossfall_neg": 550.0,
	"grade_down": 430.0,
	"grade_up": 600.0,
	"curb_t2": 340.0,
	"verge": 250.0,
}

## The #239 sweep: small locks, at speed, at several places on the lap.
##
## Chosen to span what the circuit has rather than to find a failure, and four of
## the five are on **straight** road so that a departure cannot be confused with a
## kart declining to follow a corner it was never steered into. The fifth is beside
## the reference kerb, which is in a corner by construction — every kerb on this
## circuit is.
const SWEEP_STATIONS := {
	"main straight +0.8pc": 1330.0,
	"descent -4.6pc": 460.0,
	"climb +4.6pc": 620.0,
	"crossfall -2.2pc": 200.0,
	"T2 kerb entry": 340.0,
}

## Locks swept, as a fraction of full lock. Anthony's report is "the smallest stick
## input at speed"; the measurement that followed it swept 0.16-0.40 through the
## `x^3` curve, which lands here as 0.004-0.064 of lock. These bracket that and go
## above it.
const SWEEP_LOCKS: PackedFloat32Array = [0.01, 0.02, 0.05, 0.10, 0.20]

## Entry speed for the sweep, km/h. Anthony reached 137 km/h; the brief asks for
## 110-140.
const SWEEP_KMH := 130.0

## How far apart `--case=zero` drops the kart round the lap, meters.
##
## 25 m puts 55 samples on Valdirone, which is fine enough that no corner entry,
## crossfall ramp or width taper is missed — the shortest authored feature on this
## circuit is T2's 24 m apex kerb — and coarse enough that the whole lap is one
## process of about a minute.
const ZERO_SPACING := 25.0

## Where `--case=departure` traces by default.
##
## **Set from `--case=zero`'s own output**, not chosen. 300 m is on the -4.60%
## descent to T2 with 25 m of straight left, so a zero-steering run at 130 km/h
## reaches the corner, crosses the white line, crosses the verge, crosses fourteen
## meters of asphalt run-off apron and then reaches the gravel — which is the whole
## sequence in one trace, and the sequence is the finding.
const DEPARTURE_STATION := 300.0

## How often `--case=departure` prints a trace row. 12 at 120 Hz is 10 Hz.
const TRACE_INTERVAL := 12

var _args := {}
var _case := "calibrate"
var _ground := "circuit"
var _break := ""

var _track: KartTrack
var _world: Node3D
var _terrain: TrackTerrain
var _corridor: TrackCorridor
## The height field's own grid, kept because `TrackTerrain` publishes `height_at`
## and `roughness` but not the lattice they are defined on. Recomputed from the
## arguments this file passed to `build()` rather than reached for through an
## underscore, so `--case=step` uses the public surface only.
var _terrain_cell := 0.0
var _terrain_cells := 0
var _terrain_origin := Vector2.ZERO

var _rig: KartRig
var _kart: KartBody

## The queue of runs and where we are in it.
var _runs: Array[Dictionary] = []
var _run := -1
var _tick := 0
var _started := false
var _measure := {}
var _results: Array[Dictionary] = []

## Verdicts: one row per check, `{name, passed, detail}`.
var _checks: Array[Dictionary] = []
## What a `--break` mode has to leave its fingerprint on. See `_verdict`.
var _fingerprint := ""


func _initialize() -> void:
	_args = Cmdline.parse()
	_case = Cmdline.as_string(_args, "case", "calibrate")
	_ground = Cmdline.as_string(_args, "ground", "circuit")
	_break = Cmdline.as_string(_args, "break", "")
	if _break == "true":
		printerr("--break needs a mode: input, flatground, nocurb, noterrain, calib")
		quit(2)
		return

	if not ClassDB.class_exists("KartStateHash"):
		printerr("KartStateHash is not registered — build the extension: scons target=editor")
		quit(2)
		return

	_track = KartTrack.new()
	if _track.load(TRACK_PATH) != OK:
		printerr("\n".join(_track.problems()))
		quit(2)
		return
	if not _track.select_layout(Cmdline.as_string(_args, "layout", "forward")):
		printerr("no such layout")
		quit(2)
		return

	# `--case=survey` reads the file and never builds a world, so it is answered
	# before anything is instantiated.
	if _case == "survey":
		_survey()
		quit(0)
		return

	_world = Node3D.new()
	_world.name = "TerrainWorld"
	get_root().add_child(_world)
	if _ground == "flat":
		_build_flat()
	else:
		_build_circuit()

	_runs = _plan()
	if _runs.is_empty() and _case != "place" and _case != "section" and _break == "":
		# A sabotage that leaves nothing to drive is still a sabotage and still owes a
		# fingerprint — `--break=noterrain` deletes the height field and therefore the
		# only case that can find its worst step — so an empty plan under `--break` goes
		# to the verdict rather than out the error door.
		printerr("no runs for --case=%s --ground=%s" % [_case, _ground])
		quit(2)
		return

	print("case %s, ground %s, %d run(s) at %d Hz%s" % [
		_case, _ground, _runs.size(), Engine.physics_ticks_per_second,
		"" if _break == "" else "   BREAK=" + _break,
	])


# --- the world ----------------------------------------------------------------


## The circuit's collision, built the way `circuit.gd` builds it and from the same
## two calls. Nothing visual, no session, no profile I/O — every worktree shares one
## `user://` and a probe that opens it overwrites the real career.
func _build_circuit() -> void:
	var box := _centerline_bounds()
	var extent: float = maxf(box.size.x, box.size.z) + RUNOFF_MARGIN * 2.0
	var centre := box.get_center()
	var floor_y: float = box.position.y - 0.5

	# `--break=flatground` is the control that proves the rest of this function is
	# load-bearing: it substitutes one flat asphalt slab for the whole circuit, and
	# every case's own verdict has to notice.
	if _break == "flatground":
		_world.add_child(_slab("FakeGround", 0,
			Vector3(extent, GROUND_THICKNESS, extent),
			Vector3(centre.x, -GROUND_THICKNESS * 0.5, centre.z)))
		return

	for entry in _track.surface_meshes():
		var surface_name: String = entry["name"]
		if _break == "nocurb" and surface_name == "Kerbs":
			continue
		_world.add_child(_static_body(surface_name, entry["faces"], int(entry["surface_type"])))

	var slab_top := floor_y
	if _break != "noterrain":
		var cell := Cmdline.as_float(_args, "terrain-cell", 5.0)
		_corridor = TrackCorridor.new()
		_corridor.measure(_track)
		_terrain = TrackTerrain.new()
		_terrain.build(_track, _corridor, extent, centre, floor_y, cell)
		_world.add_child(_terrain.body(2))
		slab_top = _terrain.base_y()
		# `TrackTerrain.build`'s own first three lines, recomputed from the same
		# arguments. It does not publish the lattice and this file does not reach
		# through the underscore for it.
		_terrain_cell = maxf(cell, 1.0)
		_terrain_cells = maxi(2, int(ceil(extent / _terrain_cell)))
		_terrain_origin = Vector2(centre.x - extent * 0.5, centre.z - extent * 0.5)

	_world.add_child(_slab("Ground", 2,
		Vector3(extent, GROUND_THICKNESS, extent),
		Vector3(centre.x, slab_top - GROUND_THICKNESS * 0.5, centre.z)))


## `proving_ground.gd`'s ground and nothing else. The calibration fixture.
##
## `--break=calib` tilts it into a 1% **grade along the direction of travel**, and
## the axis is not arbitrary. A 1% *cross* slope is 0.098 m/s² sideways on a kart
## driving in a straight line and moves neither the top speed nor the 0-100, so a
## control built that way is inert — the same defect `replay.sh` shipped, where the
## strongest of five negative controls could not fire at the tick the script
## actually passed. A 1% grade is 16.7 N against the drive and 0.098 m/s² out of an
## average of 6.3, which is 1.5% on the 0-100 and cannot hide inside a 0.5%
## tolerance.
const BREAK_CALIB_GRADE := 0.01


func _build_flat() -> void:
	var body := _slab("Ground", 0,
		Vector3(FLAT_SIZE, FLAT_THICKNESS, FLAT_SIZE),
		Vector3(0.0, -FLAT_THICKNESS * 0.5, 0.0))
	if _break == "calib":
		body.rotation.x = atan(BREAK_CALIB_GRADE)
	_world.add_child(body)


func _slab(node_name: String, surface: int, size: Vector3, at: Vector3) -> StaticBody3D:
	var body := StaticBody3D.new()
	body.name = node_name
	# Friction is the tire model's. `KartBody::_ready` sets its own body's friction
	# to 0.0 and the combine rule is `min(a, b)` — measured, ADR-0033 — so whatever
	# is written here reaches nothing. The number that matters is the surface type.
	var material := PhysicsMaterial.new()
	material.friction = 1.0
	material.rough = true
	body.physics_material_override = material
	body.set_meta("surface_type", surface)
	var shape := BoxShape3D.new()
	shape.size = size
	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.position = at
	collider.name = node_name + "Shape"
	body.add_child(collider)
	return body


## `circuit.gd`'s `_static_body`, including `backface_collision`: a road built as a
## surface rather than as a volume has a failure mode a slab does not — anything
## that ends up underneath it falls through the world.
func _static_body(node_name: String, faces: PackedVector3Array, surface: int) -> StaticBody3D:
	var body := StaticBody3D.new()
	body.name = node_name
	var material := PhysicsMaterial.new()
	material.friction = 1.0
	material.rough = true
	body.physics_material_override = material
	body.set_meta("surface_type", surface)
	var shape := ConcavePolygonShape3D.new()
	shape.set_faces(faces)
	shape.backface_collision = true
	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.name = node_name + "Shape"
	body.add_child(collider)
	return body


func _centerline_bounds() -> AABB:
	var box := AABB()
	var started := false
	for point in _track.centerline(0.2, 8.0):
		if not started:
			box = AABB(point, Vector3.ZERO)
			started = true
		else:
			box = box.expand(point)
	return box


# --- placement ----------------------------------------------------------------


## Where the road actually is at (station, lateral), found by raycasting into the
## collision that was just built.
##
## Deliberately **not** a second evaluation of `docs/TRACK_SCHEMA.md`'s
## cross-section formula. `KartTrack::_surface_point` is that formula and
## `grid_transform` is that formula turned into a basis; a probe that reimplemented
## either would agree with the track by construction and could not detect a
## collider that had drifted from it. Casting a ray asks the physics server what is
## under a point, which is the same question `KartBody::query_ground` asks four
## times a tick.
##
## Returns `{}` when nothing is under the point, which is a real answer — it is what
## the far side of a run-off apron looks like without the height field.
func _place(station: float, lateral: float) -> Dictionary:
	var frame := _track.sample(station)
	var heading: float = frame["heading"]
	# `src/core/track.h`'s `forward_of` and `right_of`, which is the one place the
	# convention is written down: forward is (sin h, -cos h) and right is (cos h,
	# sin h). Getting this wrong puts the kart across the road instead of along it,
	# and it is invisible in a number.
	var forward := Vector3(sin(heading), 0.0, -cos(heading))
	var right := Vector3(cos(heading), 0.0, sin(heading))
	var centre: Vector3 = frame["position"]
	var xz := centre + right * lateral

	var space := _world.get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(
		Vector3(xz.x, centre.y + PLACE_RAY_LIFT, xz.z),
		Vector3(xz.x, centre.y + PLACE_RAY_LIFT - PLACE_RAY_LENGTH, xz.z))
	var hit := space.intersect_ray(query)
	if hit.is_empty():
		return {}

	var normal: Vector3 = hit["normal"]
	# A ray that entered a backface — the underside of a run-off apron, say — comes
	# back with a normal pointing down. Flipping it here would silently place the
	# kart upside down under the road; refusing is the honest answer.
	if normal.y <= 0.0:
		return {}
	var point: Vector3 = hit["position"]

	var back := -(forward - normal * forward.dot(normal)).normalized()
	var side := normal.cross(back).normalized()
	var placed := Transform3D(Basis(side, normal, back), point + normal * SPAWN_LIFT)

	var collider: Object = hit["collider"]
	return {
		"transform": placed,
		"normal": normal,
		"point": point,
		"surface": int(collider.get_meta("surface_type", 0)) if collider != null else 0,
		"body": String(collider.name) if collider != null else "",
		"grade": float(frame["grade"]),
		"bank_pct": float(frame["bank_pct"]),
		"crown_pct": float(frame["crown_pct"]),
		"width": float(frame["width"]),
		"curvature": float(frame["curvature"]),
	}


# --- the run plan -------------------------------------------------------------


## One run: where it starts, how fast, how long, and what the driver does.
##
## `input` is a `Callable(second: float) -> Dictionary` and nothing else — a pure
## function of elapsed time, so the run is reproducible by construction and the
## state hash means something.
func _spec(run_name: String, at: Variant, kmh: float, gear: int, ticks: int,
		input: Callable, extra := {}) -> Dictionary:
	var spec := {
		"name": run_name,
		"at": at,
		"kmh": kmh,
		"gear": gear,
		"ticks": ticks,
		"input": input,
	}
	spec.merge(extra)
	return spec


## Full throttle from rest, straight. `drive_probe.gd`'s `accel`.
func _input_accel() -> Callable:
	return func(_second: float) -> Dictionary:
		return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}


## Accelerate for 6 s, then stand on the brake. `drive_probe.gd`'s `brake`.
func _input_brake() -> Callable:
	return func(second: float) -> Dictionary:
		if second < 6.0:
			return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
		return {"throttle": 0.0, "brake": 1.0, "steer": 0.0}


## Hold a constant. Straight-line cases: the ground is the variable.
func _input_hold(throttle: float, brake: float, steer: float) -> Callable:
	return func(_second: float) -> Dictionary:
		return {"throttle": throttle, "brake": brake, "steer": steer}


## Coast until `at_second`, then stand on the brake.
##
## The delay is not cosmetic: `SETTLE_TICKS` of the run is a kart dropping 0.12 m
## onto the road, and a scenario that brakes through that window has already gone
## past 90 km/h before the 90-20 measurement is allowed to start — which silently
## turns "mean deceleration from 90" into "mean deceleration from 78".
func _input_brake_at(at_second: float) -> Callable:
	return func(second: float) -> Dictionary:
		if second < at_second:
			return {"throttle": 0.0, "brake": 0.0, "steer": 0.0}
		return {"throttle": 0.0, "brake": 1.0, "steer": 0.0}


## Settle straight, then a step to `lock` and hold it. The #239 scenario.
##
## The step is a step and not a ramp on purpose: `steering.h`'s own rate limiter is
## what turns it into a ramp, and putting a second ramp in front of it would be this
## file modelling the driver.
func _input_step_steer(lock: float, at_second: float, throttle: float) -> Callable:
	return func(second: float) -> Dictionary:
		if second < at_second:
			return {"throttle": throttle, "brake": 0.0, "steer": 0.0}
		return {"throttle": throttle, "brake": 0.0, "steer": lock}


## The same run, on whichever ground this process is driving.
##
## **Every ground case is a matched pair and that is the whole method.** A number
## measured on 4.6% of grade means nothing on its own; what it means is the
## difference from the identical scenario on a plane, and the only way to keep the
## scenario identical is to emit it from one place. `--ground=flat` substitutes
## `proving_ground.gd`'s spawn for the station and changes nothing else — same
## entry speed, same gear, same tick budget, same input Callable.
func _paired(run_name: String, station: float, kmh: float, gear: int, ticks: int,
		input: Callable, extra := {}) -> Dictionary:
	var with_settle := extra.duplicate()
	if not with_settle.has("settle"):
		with_settle["settle"] = GROUND_SETTLE_TICKS
	if _ground == "flat":
		# A lateral offset is an offset from a road edge that a plane does not have.
		with_settle.erase("lateral")
		return _spec(run_name, FLAT_SPAWN, kmh, gear, ticks, input, with_settle)
	return _spec(run_name, station, kmh, gear, ticks, input, with_settle)


## This run's settle window, in ticks.
func _settle() -> int:
	return int(_runs[_run].get("settle", SETTLE_TICKS))


func _plan() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var settle_s := float(SETTLE_TICKS) / float(Engine.physics_ticks_per_second)
	match _case:
		"calibrate":
			# The whole point: the same code, on a plane, has to give `drive.sh`'s
			# numbers back. Flat ground only — on the circuit there is nowhere to run it.
			if _ground != "flat":
				return out
			out.append(_spec("accel", FLAT_SPAWN, 0.0, 0, 1200, _input_accel()))
			out.append(_spec("brake", FLAT_SPAWN, 0.0, 0, 1200, _input_brake()))
		"place", "section":
			# Neither drives: `place` is the placement raycast against `grid_transform`,
			# `section` is a lateral cross-section of what the collision actually is.
			pass
		"sixfour":
			# **A matched pair, run twice: once on the plane, once on the road.** The
			# accel scenario is `drive.sh`'s verbatim, so the circuit column can be read
			# against `drive.sh` directly — and it runs out of road, which is the answer
			# and is reported as `left road at`.
			#
			# The brake scenario is **not** `drive.sh`'s. `drive.sh` accelerates for 6 s
			# first, which on this circuit is 160 m of grass before the brake is even
			# touched, so the figure would describe a kart braking on a lawn. Entering at
			# 120 km/h under `engage` instead puts the whole 90-20 km/h measurement on
			# asphalt, and the flat run uses the identical entry so the two are
			# comparable to each other as well as to §6.4.
			var accel_at: Variant = FLAT_SPAWN if _ground == "flat" else SIXFOUR_STATION
			var tag := "flat" if _ground == "flat" else "circuit"
			# `_spec` and not `_paired`, so these two keep `SETTLE_TICKS` — they are the
			# rows compared against `drive.sh` and the averaging window is part of the
			# figure.
			out.append(_spec("accel " + tag, accel_at, 0.0, 0, 1200, _input_accel()))
			out.append(_spec("brake 130 " + tag, accel_at, 130.0, 6, 480,
				_input_brake_at(settle_s), {"no_zero_to_100": true}))
		"slope":
			# Straight-line, full throttle and full brake, on the two steepest bits of
			# road. A grade is a longitudinal force the plane has never applied, and the
			# figure that carries it is `mean long g` against the flat pair.
			#
			# **300 ticks, which is 2.5 s and about 70 m.** The first cut ran 600 and the
			# descent case drove straight into T3's run-off at 73 km/h, hit the barrier
			# and reported 105.95 g of lateral and 51 ticks of air. That is a real
			# measurement of something and it is not a measurement of a grade — a
			# straight-line scenario has to end before the road stops being straight.
			out.append(_paired("accel down -4.6pc", CASE_STATIONS["grade_down"], 100.0, 5, 300,
				_input_accel(), {"no_zero_to_100": true}))
			out.append(_paired("accel up +4.6pc", CASE_STATIONS["grade_up"], 100.0, 5, 300,
				_input_accel(), {"no_zero_to_100": true}))
			out.append(_paired("brake down -4.6pc", CASE_STATIONS["grade_down"], 100.0, 5, 300,
				_input_brake_at(settle_s), {"no_zero_to_100": true}))
			out.append(_paired("brake up +4.6pc", CASE_STATIONS["grade_up"], 100.0, 5, 300,
				_input_brake_at(settle_s), {"no_zero_to_100": true}))
		"crossfall":
			# Straight-line at speed on the worst cross slopes. Nothing is asked of the
			# steering: whatever the kart does here it does because the road is tilted
			# under it. Short, for the reason above and one more — the two worst are
			# inside corner arcs, because crossfall exists to bank a corner.
			# **The two worst crossfalls are run at 30 km/h, and the reason is
			# arithmetic rather than caution.** Both are inside corner arcs, because
			# crossfall is what banks a corner — T5 is 22 m of radius and T3 is 15 —
			# and a kart running straight leaves the road at `v^2 t^2 / 2R`. At 100 km/h
			# T5's 5 m of half-width is gone in 0.53 s, which is inside `SETTLE_TICKS`:
			# the run is off the road before the measurement window opens, and every
			# figure it prints describes gravel. At 30 km/h the same drift takes 1.78 s
			# and the whole run stays on asphalt.
			#
			# What is being measured here — the attitude the road imposes, the roll, the
			# lateral component of gravity across a tilted contact patch — does not
			# depend on road speed, so the low entry costs nothing. What *cannot* be
			# measured straight-line at speed on this circuit is the same crossfall
			# under 100 km/h of load, and no scenario in this file claims to.
			out.append(_paired("+8.0pc T5 Vigna", CASE_STATIONS["crossfall_max"], 30.0, 2, 180,
				_input_hold(0.2, 0.0, 0.0), {"no_zero_to_100": true}))
			out.append(_paired("-7.0pc T3 Il Pozzo", CASE_STATIONS["crossfall_neg"], 30.0, 2, 180,
				_input_hold(0.2, 0.0, 0.0), {"no_zero_to_100": true}))
			# The worst crossfall the circuit has on **straight** road, which is where it
			# can be held: 3.894% at station 849, measured by `--case=survey`.
			out.append(_paired("-3.9pc straight 849", 849.0, 100.0, 5, 270,
				_input_hold(0.5, 0.0, 0.0), {"no_zero_to_100": true}))
			out.append(_paired("-2.2pc straight 200", 200.0, 130.0, 6, 270,
				_input_hold(0.5, 0.0, 0.0), {"no_zero_to_100": true}))
			# The 2% crown, on the centerline, where a kart straddles the ridge with its
			# left wheels on -2% and its right wheels on +2%. It is on every meter of
			# this circuit and it is the term the flat-plane rigs forget.
			out.append(_paired("2pc crown, centerline", 1330.0, 130.0, 6, 270,
				_input_hold(0.5, 0.0, 0.0), {"no_zero_to_100": true}))
		"curb":
			# T2 Lama's inside kerb is on the **right**, from 344.306 m, 1.0 m wide and
			# 30 mm high — `track.json` calls it "the reference kerb". The kart starts
			# 4 m before it at the corner's own 129.0 km/h apex speed, offset so its
			# right-hand wheels straddle the kerb line and then sit on it.
			#
			# The flat pair runs the same speed and the same input on plain asphalt, so
			# the columns that move are the kerb's doing and not the corner's.
			# A kerb is built outward from the white line: `kart_track.cpp` puts its top
			# quad over lateral [edge, edge + width] at `height_m`, with a vertical face
			# at `edge` itself — "the face a wheel climbs, and the whole reason this is a
			# kerb rather than a painted strip". So `half - 0.35` straddles it and
			# `half + 0.5` puts the right pair squarely on it.
			var half := float(_track.sample(CASE_STATIONS["curb_t2"])["width"]) * 0.5
			out.append(_paired("T2 apex kerb strike 129", CASE_STATIONS["curb_t2"], 129.0, 6, 180,
				_input_hold(0.4, 0.0, 0.0), {"lateral": half - 0.35, "no_zero_to_100": true}))
			out.append(_paired("T2 apex kerb, 2 wheels", CASE_STATIONS["curb_t2"], 129.0, 6, 180,
				_input_hold(0.4, 0.0, 0.0), {"lateral": half + 0.5, "no_zero_to_100": true}))
			# **Ridden rather than struck**, and it has to be slow for the same reason
			# the crossfall rows are: T2 is 42 m of radius, so a straight line leaves its
			# 10 m of road in 0.80 s at the corner's own 129 km/h apex speed. At 50 km/h
			# the kerb can be held for two full seconds, which is what "riding a kerb"
			# means and what the 30 mm face and the 0.72 grip multiplier are there for.
			out.append(_paired("T2 apex kerb ridden 50", CASE_STATIONS["curb_t2"], 50.0, 3, 300,
				_input_hold(0.2, 0.0, 0.0), {"lateral": half + 0.5, "no_zero_to_100": true}))
			# **Running wide onto the exit kerb**, which is the one kerb on this circuit
			# a straight line arrives at rather than departs from: T2 is a right-hander
			# and the 20 mm flat strip at 367.460-402.460 m is on its **left**, so the
			# outward drift carries the kart onto it. `track.json`'s own note for that
			# span says "running wide is a..." and this is the run that finishes the
			# sentence.
			# **Station 370 and lateral -3.0, and both are arithmetic.** The outward
			# drift is `v^2 t^2 / 2R`, which at 129 km/h in T2's 42 m arc is 15.26 t².
			# The left wheels reach the kerb's inner edge when the chassis has drifted
			# 1.4 m, at t = 0.30 s — just past `GROUND_SETTLE_TICKS` — and are past its
			# outer edge by 0.56 s.
			#
			# **Placed at 370 rather than at 380 because the first cut raced the kerb's
			# own end and lost.** The span runs 367.460 to 402.460 m; from 380 at
			# 35.8 m/s the kart is at station 401 by the time it has drifted far enough
			# laterally, which is inside `_ramp`'s taper where the kerb height is on its
			# way to zero. It reported **0.0% kerb and 64.8% grass**, which reads exactly
			# like a missing collider — `--case=section --station=380` is what showed the
			# kerb is right there at lateral -5.9 to -4.9 and the scenario was the thing
			# that was wrong.
			out.append(_paired("T2 exit kerb, run wide", 370.0, 129.0, 6, 300,
				_input_hold(0.4, 0.0, 0.0), {"lateral": -3.0, "no_zero_to_100": true}))
			out.append(_paired("T2 apex kerb + steer .05", CASE_STATIONS["curb_t2"], 129.0, 6, 180,
				_input_step_steer(0.05, settle_s + 0.25, 0.4),
				{"lateral": half - 0.35, "no_zero_to_100": true}))
		"verge":
			# Two wheels off the edge at speed, on an unbanked straight so the only
			# variable is the surface. 250 m is 11.0 m wide; the offsets put the right
			# pair a little over the white line, then all four well over it.
			var edge := float(_track.sample(CASE_STATIONS["verge"])["width"]) * 0.5
			# **The on-asphalt control, and it is the row that makes the other three
			# mean anything.** Same station, same speed, same input, 1.0 m *inside* the
			# white line — so every wheel stays on asphalt. If this row also departs then
			# the departure is the kart or the placement and has nothing to do with the
			# verge, and the whole case is void. It is deliberately the first row.
			out.append(_paired("inside the line (control)", CASE_STATIONS["verge"], 130.0, 6, 360,
				_input_hold(0.6, 0.0, 0.0), {"lateral": edge - 1.6, "no_zero_to_100": true}))
			out.append(_paired("right wheels on verge", CASE_STATIONS["verge"], 130.0, 6, 360,
				_input_hold(0.6, 0.0, 0.0), {"lateral": edge - 0.30, "no_zero_to_100": true}))
			out.append(_paired("all four on verge", CASE_STATIONS["verge"], 130.0, 6, 360,
				_input_hold(0.6, 0.0, 0.0), {"lateral": edge + 1.2, "no_zero_to_100": true}))
			out.append(_paired("verge, then steer 0.05", CASE_STATIONS["verge"], 130.0, 6, 360,
				_input_step_steer(0.05, settle_s + 0.25, 0.6),
				{"lateral": edge - 0.30, "no_zero_to_100": true}))
			# **The full excursion**, which is the case the other three build up to and
			# the one a driver actually reaches: straight on at T3's entry, across the
			# verge, onto the gravel trap and into the barrier. It is here rather than in
			# `--case=crossfall` because what it measures is the run-off, and it is worth
			# a row of its own because it is the only run in this file that reaches a
			# `Barriers` triangle at all.
			out.append(_paired("straight off T3 at 90", CASE_STATIONS["crossfall_neg"], 90.0, 4,
				600, _input_hold(0.5, 0.0, 0.0), {"no_zero_to_100": true}))
		"step":
			if _ground != "circuit":
				return out
			# The terrain height field's worst cell-to-cell step, found rather than
			# assumed — `_worst_terrain_step` walks the field the same way
			# `TrackTerrain`'s own docstring measures it.
			var worst := _worst_terrain_step()
			if worst.is_empty():
				return out
			out.append(_spec("into the %.3f m step" % float(worst["step"]),
				worst["from"], 90.0, 4, 600, _input_hold(0.3, 0.0, 0.0),
				{"world": true, "heading_to": worst["to"], "step_m": worst["step"]}))
			out.append(_spec("off the %.3f m step" % float(worst["step"]),
				worst["to"], 90.0, 4, 600, _input_hold(0.3, 0.0, 0.0),
				{"world": true, "heading_to": worst["from"], "step_m": worst["step"]}))
		"zero":
			# **Agent B's #239 result, asked of the whole lap.** B measured the kart
			# spinning on Valdirone with `input.steer` bit-identical 0.000000000 while
			# the same rig ran 0.079 deg of body slip on the proving ground. If the
			# steering is not the cause then the ground is, and the first question is
			# *which* ground: this drops the kart at 130 km/h every `ZERO_SPACING`
			# meters round the lap, full throttle, no steering at all, and reports which
			# stations depart.
			#
			# `slip on road` is the column that matters. A straight-line run leaves the
			# road at every corner by construction, and a kart that has left the road is
			# on grass or gravel and is *expected* to misbehave. What B found is a kart
			# spinning with all four wheels on hard surface, and only the on-road column
			# can tell those two apart.
			# On a plane every station is the same station, so the control is **one**
			# run and not 55 identical ones. It is the row agent B measured at 0.079 deg
			# of body slip while the circuit gave 178.778 for the same input.
			if _ground == "flat":
				out.append(_paired("flat plane control", 0.0, 130.0, 6, 300,
					_input_hold(1.0, 0.0, 0.0), {"no_zero_to_100": true}))
			else:
				var zero_at := 0.0
				while zero_at < _track.length():
					out.append(_paired("station %4.0f" % zero_at, zero_at, 130.0, 6, 300,
						_input_hold(1.0, 0.0, 0.0), {"no_zero_to_100": true}))
					zero_at += ZERO_SPACING
		"departure":
			# One station, traced tick by tick, so whatever `--case=zero` finds can be
			# watched happening. `--station=`, `--entry=`, `--throttle=`, `--steer=` and
			# `--ticks=` are all levers here on purpose: B's third row — the same section
			# coasting rather than at full throttle — yawed 13.2 deg and recovered, and
			# that is the difference between "the ground triggers it" and "the drivetrain
			# amplifies it". Reproducing it needs the throttle to be an argument.
			out.append(_paired("departure trace",
				Cmdline.as_float(_args, "station", DEPARTURE_STATION),
				Cmdline.as_float(_args, "entry", 130.0), 6,
				Cmdline.as_int(_args, "ticks", 360), _input_hold(
					Cmdline.as_float(_args, "throttle", 1.0), 0.0,
					Cmdline.as_float(_args, "steer", 0.0)),
				{"no_zero_to_100": true, "trace": true}))
		"sweep":
			# The #239 question. On flat ground the station is irrelevant — the input is
			# a pure function of time and the plane is the same everywhere — so the flat
			# column is one run per lock and not one per station.
			# 480 ticks is 4.0 s: 0.75 s of settling, 0.25 s straight, then 3.0 s of held
			# lock. At 130 km/h that is 108 m after the step, which is inside the longest
			# straight and is four times the 0.7 s a yaw departure needs to become
			# visible. Longer would only add road the kart has already left.
			if _ground == "flat":
				for lock in SWEEP_LOCKS:
					out.append(_spec("lock %.2f" % lock, FLAT_SPAWN, SWEEP_KMH, 6, 480,
						_input_step_steer(lock, settle_s + 0.25, 0.5), {"lock": lock}))
			else:
				for where in SWEEP_STATIONS:
					for lock in SWEEP_LOCKS:
						out.append(_spec("%s lock %.2f" % [where, lock],
							SWEEP_STATIONS[where], SWEEP_KMH, 6, 480,
							_input_step_steer(lock, settle_s + 0.25, 0.5),
							{"lock": lock, "where": where}))
	return out


# --- the loop -----------------------------------------------------------------


func _physics_process(delta: float) -> bool:
	if not _started and (_case == "place" or _case == "section" or _runs.is_empty()):
		# `--case=place` drives nothing and an empty plan under `--break` has nothing
		# to drive, but both still need one tick for the static bodies to reach the
		# space before a placement ray is cast at them.
		_started = true
		return false
	if _case == "place" or _case == "section" or _runs.is_empty():
		return _finish()

	if not _started:
		# One tick of nothing, so the static bodies are in the space before the first
		# placement ray is cast. A `--script` main loop's scene is parented but not
		# `_ready` when `_initialize` returns; CLAUDE.md's trap list has this shape.
		_started = true
		return false

	if _run < 0 or _tick >= int(_runs[_run]["ticks"]):
		if _run >= 0:
			_end_run()
		_run += 1
		if _run >= _runs.size():
			return _finish()
		if not _begin_run(_runs[_run]):
			return _finish()
		return false

	_sample(delta)
	_tick += 1
	return false


func _begin_run(spec: Dictionary) -> bool:
	_tick = 0
	_measure = _fresh_measure()

	var at: Variant = spec["at"]
	var placed := {}
	if _ground == "flat":
		# The tilted plane's top face is no longer at y = 0 away from the origin, so
		# the spawn is rotated with it — otherwise the kart is dropped from 10 m and
		# `--break=calib` goes red for a reason it did not cause, which is exactly the
		# coincidence a negative control is not allowed to accept.
		var tilt := Basis() if _break != "calib" else Basis(Vector3.RIGHT, atan(BREAK_CALIB_GRADE))
		placed = {
			"transform": Transform3D(tilt, tilt * (at as Vector3)),
			"normal": tilt.y, "point": tilt * ((at as Vector3) - Vector3(0.0, SPAWN_LIFT, 0.0)),
			"surface": 0, "body": "Ground",
			"grade": 0.0, "bank_pct": 0.0, "crown_pct": 0.0, "width": 0.0, "curvature": 0.0,
		}
	elif spec.get("world", false):
		placed = _place_world(at as Vector3, spec["heading_to"] as Vector3)
	else:
		placed = _place(at as float, float(spec.get("lateral", 0.0)))

	if placed.is_empty():
		printerr("nothing under the spawn point for run '%s'" % spec["name"])
		return false
	_measure["placed"] = placed

	_rig = KartRig.new()
	# `listener = false`: Godot has exactly one audio listener and this probe has no
	# audio device. `ai = false`: an `AiDriver` would be a controller, and every input
	# here is an open-loop function of the tick counter on purpose.
	_kart = _rig.build(_world, placed["transform"], {}, false, false)
	if _kart == null:
		printerr("KartRig built no kart — is assets/generated/kart.glb present?")
		return false

	# **`input_driver` is what makes the scripted input reach the solver**, and
	# `--break=input` is the control that proves it: with the Callable unset the body
	# takes whatever `PlayerDriver` pushed, which headless is neutral, and every
	# assertion below fires.
	if _break != "input":
		_kart.input_driver = func() -> Dictionary:
			return _commanded()

	# The drivetrain and the body put into a consistent state at the entry speed, in
	# one call, by the solver's own helper: `KartBody::engage` sets the road speed
	# along the kart's forward and matches the driveline to it. Setting
	# `linear_velocity` alone would leave a driveline turning at zero under a chassis
	# at 36 m/s, which is one enormous slip ratio.
	if float(spec["kmh"]) > 0.0:
		_kart.engage(int(spec["gear"]), KartCore.kmh_to_ms(float(spec["kmh"])))

	_measure["previous_position"] = _kart.global_position
	_measure["previous_velocity"] = _kart.linear_velocity
	_tick = Cmdline.as_int(_args, "tick-offset", TICK_OFFSET)
	return true


## What the driver is asking for this tick. One source, read by the `input_driver`
## Callable and by the assertion, so the two can never describe different ticks.
func _commanded() -> Dictionary:
	var second := float(_tick) / float(Engine.physics_ticks_per_second)
	return (_runs[_run]["input"] as Callable).call(second)


func _fresh_measure() -> Dictionary:
	return {
		"hash": KartStateHash.new(),
		"max_body_slip": 0.0,
		# The same channel, stopped the moment the kart's chassis origin crosses the
		# white line. Valdirone's longest straight is 165 m and a 4 s run at 130 km/h
		# covers 144, so several sweep runs leave the road before they finish — and
		# "what it did while still on the circuit" is the only column that compares
		# like for like against a flat plane with no edges.
		"slip_on_road": 0.0,
		"max_roll_deg": 0.0,
		"peak_lateral_g": 0.0,
		"peak_braking_g": 0.0,
		# Mean deceleration from 90 km/h to 20 km/h, which is how a road test quotes a
		# braking figure and how §6.4's 1.5-2.0 g should be read. `drive_probe.gd`'s
		# channel, reproduced rather than re-invented, including its reason for timing
		# the two threshold crossings instead of the brake input: that measures the
		# kart and not the scenario script.
		"brake_mean_g": 0.0,
		"brake_start_speed": -1.0,
		"brake_start_tick": 0,
		"peak_vertical_g": 0.0,
		# Peak vertical g on a tick when at least one wheel was standing on a kerb.
		# Its flat pair is 0.00 by construction, so the column is the kerb's whole
		# contribution and nothing else's.
		"peak_vert_g_on_curb": 0.0,
		"top_speed": 0.0,
		# Top speed while the chassis origin was still inside the white lines, and how
		# far the run got before it crossed them. Valdirone's longest straight is
		# 165.0 m and `drive.sh`'s accel scenario needs 256 m, so a straight-line
		# scenario on this circuit ends on the grass — the figure has to say when.
		"top_speed_on_road": 0.0,
		"left_road_at_m": -1.0,
		"entry_speed": -1.0,
		"entry_tick": 0,
		"exit_speed": 0.0,
		"time_to_100": -1.0,
		"distance": 0.0,
		"wheels_down_min": 4,
		"wheels_touching_min": 4,
		"peak_lift": [0.0, 0.0, 0.0, 0.0],
		"air_ticks": 0,
		"max_air_ticks": 0,
		"surface_ticks": [0, 0, 0, 0],
		"contact_ticks": 0,
		"normal_tilt_max_deg": 0.0,
		"grip_min": 1.0,
		# **Issue #136's split, measured rather than described.** `contact` is the
		# raycast's answer, latched when a wheel is buried; `tire_contact` is what the
		# suspension did with it; `load` is what the spring is actually carrying. Agent
		# B's #239 trace found min load reaching exactly 0.0 N while all four corners
		# still reported `contact = true`, two ticks before the kart let go, so the
		# three are tracked together here and the interesting state is the one where
		# they disagree.
		"min_load_n": 1e30,
		"zero_load_ticks_with_contact": 0,
		"grade_at_min_load": 0.0,
		"max_off_road_m": 0.0,
		"left_road": false,
		"input_error": 0.0,
		"steer_reached": 0.0,
		"steer_commanded": 0.0,
		"previous_position": Vector3.ZERO,
		"previous_velocity": Vector3.ZERO,
		"previous_yaw_rate": 0.0,
	}


func _sample(delta: float) -> void:
	var velocity := _kart.linear_velocity
	var speed := velocity.length()
	var position := _kart.global_position
	var basis := _kart.global_transform.basis
	var state_hash: KartStateHash = _measure["hash"]

	if _tick % HASH_INTERVAL == 0:
		state_hash.add_int(_tick)
		state_hash.add_transform(_kart.global_transform)
		state_hash.add_vector3(velocity)
		state_hash.add_vector3(_kart.angular_velocity)

	# **The input assertion.** ADR-0040: a valid `input_driver` overrides the pushed
	# input, and getting the ordering backwards once made all four `drive.sh`
	# scenarios agree on one hash while measuring nothing. `throttle_input` and
	# `brake_input` are `last_input_`, i.e. exactly what the solver consumed this
	# tick; `steer_input` is the rate-limited position rather than the raw axis, so
	# it is checked by whether it ever reaches the command instead of tick by tick.
	var want := _commanded()
	_measure["input_error"] = maxf(_measure["input_error"], maxf(
		absf(float(want["throttle"]) - _kart.throttle_input),
		absf(float(want["brake"]) - _kart.brake_input)))
	_measure["steer_commanded"] = maxf(_measure["steer_commanded"], absf(float(want["steer"])))
	_measure["steer_reached"] = maxf(_measure["steer_reached"], absf(_kart.steer_input))

	_measure["distance"] = float(_measure["distance"]) \
		+ position.distance_to(_measure["previous_position"] as Vector3)
	_measure["previous_position"] = position
	_measure["top_speed"] = maxf(float(_measure["top_speed"]), speed)
	if not bool(_measure["left_road"]):
		_measure["top_speed_on_road"] = maxf(float(_measure["top_speed_on_road"]), speed)
	_measure["exit_speed"] = speed
	# A run that was `engage`d above 100 km/h has no 0-100 time and must not print
	# one: the first tick already satisfies the threshold and the figure would read
	# 0.017 s, which is a number describing the timestep.
	if not bool(_runs[_run].get("no_zero_to_100", false)) \
			and float(_measure["time_to_100"]) < 0.0 and speed >= KartCore.kmh_to_ms(100.0):
		_measure["time_to_100"] = float(_tick) / float(Engine.physics_ticks_per_second)

	var up := basis.y
	_measure["max_roll_deg"] = maxf(float(_measure["max_roll_deg"]),
		rad_to_deg(up.angle_to(Vector3.UP)))

	var settle := _settle()
	if _tick >= settle:
		if float(_measure["entry_speed"]) < 0.0:
			_measure["entry_speed"] = speed
			_measure["entry_tick"] = _tick
		_measure["wheels_down_min"] = mini(int(_measure["wheels_down_min"]), _kart.wheels_on_ground)
		if _kart.wheels_on_ground == 0:
			_measure["air_ticks"] = int(_measure["air_ticks"]) + 1
			_measure["max_air_ticks"] = maxi(int(_measure["max_air_ticks"]),
				int(_measure["air_ticks"]))
		else:
			_measure["air_ticks"] = 0

		# **The surface and the normal, per wheel, which is the whole reason this file
		# exists.** `wheel_report()`'s `surface` is what `KartBody::query_ground` read
		# off the collider's metadata and turned into `TireSlip::surface_grip`; its
		# `normal` is the contact normal the solver used. Indexed with `[]` and not
		# `get(key, default)` — a renamed key here would draw a zero forever, which is
		# the defect `kart_body.cpp` warns about a few lines above the Dictionary it
		# fills.
		var report := _kart.wheel_report()
		var touching := 0
		var on_curb := false
		var min_load := 1e30
		var all_contact := true
		var lift: Array = _measure["peak_lift"]
		var census: Array = _measure["surface_ticks"]
		for corner in report.size():
			var wheel: Dictionary = report[corner]
			if corner < lift.size():
				lift[corner] = maxf(float(lift[corner]), float(wheel["lift"]))
			if bool(wheel["tire_contact"]):
				touching += 1
			min_load = minf(min_load, float(wheel["load"]))
			if not bool(wheel["contact"]):
				all_contact = false
			if bool(wheel["contact"]):
				var surface := int(wheel["surface"])
				if surface >= 0 and surface < census.size():
					census[surface] = int(census[surface]) + 1
				if surface == 1:
					on_curb = true
				_measure["contact_ticks"] = int(_measure["contact_ticks"]) + 1
				var normal: Vector3 = wheel["normal"]
				if normal.length_squared() > 0.5:
					_measure["normal_tilt_max_deg"] = maxf(
						float(_measure["normal_tilt_max_deg"]),
						rad_to_deg(normal.angle_to(Vector3.UP)))
				_measure["grip_min"] = minf(float(_measure["grip_min"]), _grip_for(surface))
		_measure["wheels_touching_min"] = mini(int(_measure["wheels_touching_min"]), touching)
		_measure["on_curb_this_tick"] = on_curb
		if min_load < float(_measure["min_load_n"]):
			_measure["min_load_n"] = min_load
		if all_contact and min_load <= 0.0:
			_measure["zero_load_ticks_with_contact"] = \
				int(_measure["zero_load_ticks_with_contact"]) + 1

		# Track limits, from the same `project` the session's own limit check uses.
		# Unhinted deliberately: `SessionRunner` hints every tick for speed and this
		# probe runs once, so the exact answer costs nothing and cannot be wrong.
		if _ground == "circuit":
			var found := _track.project(position)
			var edge: float = float(_track.sample(float(found["distance"]))["width"]) * 0.5
			var over: float = absf(float(found["lateral"])) - edge
			if over > float(_measure["max_off_road_m"]):
				_measure["max_off_road_m"] = over
			if over > 0.0:
				if not bool(_measure["left_road"]):
					_measure["left_road_at_m"] = float(_measure["distance"])
				_measure["left_road"] = true

	if _tick > settle:
		var acceleration: Vector3 = (velocity - (_measure["previous_velocity"] as Vector3)) / delta
		var lateral_g := absf(acceleration.dot(basis.x)) / 9.80665
		if speed > 1.0:
			var slip := absf(atan2(velocity.dot(basis.x), -velocity.dot(basis.z)))
			_measure["max_body_slip"] = maxf(float(_measure["max_body_slip"]), slip)
			if not bool(_measure["left_road"]):
				_measure["slip_on_road"] = maxf(float(_measure["slip_on_road"]), slip)
		if speed > PEAK_SPEED_GATE:
			_measure["peak_lateral_g"] = maxf(float(_measure["peak_lateral_g"]), lateral_g)
			# Vertical in the **world** frame, because what a step or a kerb does is add
			# a vertical impulse and the body's own up axis moves with it.
			var vertical_g := absf(acceleration.y + 9.80665) / 9.80665
			_measure["peak_vertical_g"] = maxf(float(_measure["peak_vertical_g"]), vertical_g)
			if bool(_measure.get("on_curb_this_tick", false)):
				_measure["peak_vert_g_on_curb"] = maxf(
					float(_measure["peak_vert_g_on_curb"]), vertical_g)
			var forward := -basis.z
			var along := acceleration.dot(forward)
			var forward_speed := velocity.dot(forward)
			if forward_speed > PEAK_SPEED_GATE and along < 0.0:
				_measure["peak_braking_g"] = maxf(float(_measure["peak_braking_g"]),
					-along / 9.80665)
			# 90 km/h down to 20 km/h under brake, timed by the crossings.
			if _kart.brake_input > 0.0 and forward_speed > 1.0:
				if float(_measure["brake_start_speed"]) < 0.0 \
						and forward_speed <= KartCore.kmh_to_ms(90.0):
					_measure["brake_start_speed"] = forward_speed
					_measure["brake_start_tick"] = _tick
				elif float(_measure["brake_start_speed"]) > 0.0 \
						and float(_measure["brake_mean_g"]) == 0.0 \
						and forward_speed <= KartCore.kmh_to_ms(20.0):
					var elapsed := float(_tick - int(_measure["brake_start_tick"])) \
						/ float(Engine.physics_ticks_per_second)
					if elapsed > 0.0:
						_measure["brake_mean_g"] = (float(_measure["brake_start_speed"])
							- forward_speed) / elapsed / 9.80665
	_measure["previous_velocity"] = velocity
	if bool(_runs[_run].get("trace", false)) and _tick % TRACE_INTERVAL == 0:
		_trace(velocity, basis)


## One trace row: where the kart is on the road, what the road is doing there, and
## what the four corners are carrying.
##
## The columns are chosen to separate the three explanations that are on the table
## for #239's departure. `grade` and `bank` say whether the road is the trigger;
## `min load` and `4 contact` say whether the kart is unloading a corner while
## still reporting contact — agent B's trace found exactly that state two ticks
## before it let go; `tilt` is the worst contact-normal deviation, which is what a
## normal discontinuity would show up as; `surf` is the four wheels' surfaces, so a
## departure on hard surface can be told from one on grass without reading a
## separate table.
func _trace(velocity: Vector3, basis: Basis) -> void:
	var found := _track.project(_kart.global_position)
	var station := float(found["distance"])
	var frame := _track.sample(station)
	var report := _kart.wheel_report()
	var min_load := 1e30
	var contacts := 0
	var tilt := 0.0
	var surfaces := PackedStringArray()
	for wheel in report:
		min_load = minf(min_load, float(wheel["load"]))
		if bool(wheel["contact"]):
			contacts += 1
			var normal: Vector3 = wheel["normal"]
			if normal.length_squared() > 0.5:
				tilt = maxf(tilt, rad_to_deg(normal.angle_to(Vector3.UP)))
		surfaces.append(str(int(wheel["surface"])))
	var slip := 0.0
	if velocity.length() > 1.0:
		slip = rad_to_deg(atan2(velocity.dot(basis.x), -velocity.dot(basis.z)))
	if _tick <= TRACE_INTERVAL:
		print("")
		print("      s   km/h  station  lateral  over  grade%  bank%   slip  yaw d/s"
			+ "  contact  minload   tilt  surf      y")
	print("    %5.2f %6.1f %8.1f %8.2f %5.2f %7.3f %6.2f %6.1f %8.1f %8d %8.1f %6.2f  %-8s %7.2f" % [
		float(_tick) / float(Engine.physics_ticks_per_second),
		KartCore.ms_to_kmh(velocity.length()), station, float(found["lateral"]),
		maxf(0.0, absf(float(found["lateral"])) - float(frame["width"]) * 0.5),
		float(frame["grade"]) * 100.0, float(frame["bank_pct"]), slip,
		rad_to_deg(_kart.angular_velocity.dot(basis.y)), contacts,
		min_load if min_load < 1e29 else -1.0, tilt,
		"".join(surfaces), _kart.global_position.y,
	])


## `src/core/surface.h`'s `grip_for`, restated.
##
## **It is restated because nothing publishes it.** `KartCore` exposes
## `build_info`, `kz_reference` and four unit conversions and no surface table at
## all, so every GDScript consumer that wants to know what a wheel-tick on grass
## cost has to carry its own copy of four numbers whose header calls them sourced.
## This column is a report column and never reaches the simulation — the solver
## reads the real table — but the duplication is a defect and it is filed rather
## than hidden. See the report's out-of-scope section.
func _grip_for(surface: int) -> float:
	match surface:
		1: return 0.72
		2: return 0.18
		3: return 0.17
		_: return 1.00


## How many ticks a row's entry-to-exit window covers. Stored at `_end_run` because
## `_tick` has moved on by the time the report runs.
func _tick_span(row: Dictionary) -> int:
	return maxi(0, int(row["end_tick"]) - int(row["entry_tick"]))


func _end_run() -> void:
	var spec := _runs[_run]
	var row := _measure.duplicate()
	row["name"] = spec["name"]
	row["end_tick"] = _tick
	row["hash_hex"] = (_measure["hash"] as KartStateHash).hex()
	row.erase("hash")
	for key in ["lock", "where", "step_m"]:
		if spec.has(key):
			row[key] = spec[key]
	_results.append(row)

	if _rig != null:
		if _rig.driver != null:
			_rig.driver.free()
		if _kart != null:
			_kart.free()
	_rig = null
	_kart = null


# --- reporting ----------------------------------------------------------------


func _finish() -> bool:
	if not _results.is_empty():
		match _case:
			"calibrate", "sixfour":
				_report_longitudinal()
			"sweep", "zero":
				_report_sweep()
			_:
				_report_ground()
	_verdict()
	return true


func _report_longitudinal() -> void:
	var kz: Dictionary = KartCore.kz_reference()
	print("")
	print("    run                 top km/h  on road  0-100 s  brake 90-20 mean/peak g  dist m  left road at")
	for row in _results:
		print("    %-18s %9.1f %8.1f %8s %11.2f / %-9.2f %7.1f  %s" % [
			row["name"], KartCore.ms_to_kmh(float(row["top_speed"])),
			KartCore.ms_to_kmh(float(row["top_speed_on_road"])),
			"%.3f" % float(row["time_to_100"]) if float(row["time_to_100"]) >= 0.0 else "  --   ",
			float(row["brake_mean_g"]), float(row["peak_braking_g"]), float(row["distance"]),
			"%.1f m" % float(row["left_road_at_m"]) if float(row["left_road_at_m"]) >= 0.0
				else "stayed on",
		])
	print("    KZ reference       %9.0f-%.0f %19.1f-%.1f %11.1f-%.1f" % [
		kz["top_speed_min_kmh"], kz["top_speed_max_kmh"],
		kz["zero_to_100_kmh_min_s"], kz["zero_to_100_kmh_max_s"],
		kz["braking_g_min"], kz["braking_g_max"],
	])
	_report_common()


func _report_ground() -> void:
	print("")
	print("    run                       entry  exit  mean long g  slip  roll  lat g  vert g  kerb g  air  down  off m")
	for row in _results:
		# Mean longitudinal g over the measured window. On a matched pair this is the
		# column the grade lives in: the same throttle, the same gear, the same entry
		# speed, and the only difference between the two rows is what the road is doing.
		var seconds := float(_tick_span(row)) / float(Engine.physics_ticks_per_second)
		var mean_long := 0.0
		if seconds > 0.0:
			mean_long = (float(row["exit_speed"]) - float(row["entry_speed"])) / seconds / 9.80665
		print("    %-24s %6.1f %5.1f %12.3f %5.1f %5.1f %6.2f %7.2f %7.2f %4d %5d %6.2f" % [
			row["name"],
			KartCore.ms_to_kmh(float(row["entry_speed"])),
			KartCore.ms_to_kmh(float(row["exit_speed"])),
			mean_long,
			rad_to_deg(float(row["max_body_slip"])),
			float(row["max_roll_deg"]),
			float(row["peak_lateral_g"]),
			float(row["peak_vertical_g"]),
			float(row["peak_vert_g_on_curb"]),
			int(row["max_air_ticks"]),
			int(row["wheels_down_min"]),
			float(row["max_off_road_m"]),
		])
	# **A run that hit something publishes no cornering figure**, and the marker is
	# here so that nobody quotes one. Several run-off rows read 100-140 g of lateral;
	# that is a barrier, not a tire, and `drive_probe.gd`'s own header records what
	# happened the last time a number off a departed run went into `REFERENCES.md`.
	# Barriers carry `surface_type` 0 by design — a barrier is asphalt-hard — so they
	# cannot be told apart by the surface census, only by the magnitude.
	var collided := false
	for row in _results:
		if float(row["peak_lateral_g"]) > COLLISION_G:
			collided = true
			print("    %-24s COLLIDED: %.1f g lateral is a barrier, not a tire — no" % [
				row["name"], float(row["peak_lateral_g"])])
			print("    %-24s           cornering figure may be read off this row." % "")
	if collided:
		print("")
	_report_common()


func _report_sweep() -> void:
	print("")
	print("    run                            lock  entry km/h  slip on road  slip max  peak lat g  min grip  min load N  0-load ticks  off m")
	for row in _results:
		print("    %-30s %4.2f %11.1f %13.2f %9.2f %11.2f %9.2f %11.1f %13d %6.2f" % [
			row["name"], float(row.get("lock", 0.0)),
			KartCore.ms_to_kmh(float(row["entry_speed"])),
			rad_to_deg(float(row["slip_on_road"])),
			rad_to_deg(float(row["max_body_slip"])),
			float(row["peak_lateral_g"]),
			float(row["grip_min"]),
			float(row["min_load_n"]) if float(row["min_load_n"]) < 1e29 else -1.0,
			int(row["zero_load_ticks_with_contact"]),
			float(row["max_off_road_m"]),
		])
	_report_common()


## The columns every case wants and none of them is about the scenario: what the
## wheels were standing on, how far the contact normal tilted, and whether the
## input the solver consumed was the input this file asked for.
func _report_common() -> void:
	print("")
	print("    run                       asphalt  curb  grass  dirt   tilt deg  in-err  steer cmd/got")
	for row in _results:
		var census: Array = row["surface_ticks"]
		var total: int = maxi(1, int(row["contact_ticks"]))
		print("    %-24s %7.1f%% %4.1f%% %5.1f%% %4.1f%% %9.2f %7.4f   %.2f / %.2f" % [
			row["name"],
			100.0 * float(census[0]) / total, 100.0 * float(census[1]) / total,
			100.0 * float(census[2]) / total, 100.0 * float(census[3]) / total,
			float(row["normal_tilt_max_deg"]), float(row["input_error"]),
			float(row["steer_commanded"]), float(row["steer_reached"]),
		])
	print("")
	for row in _results:
		print("state-hash %-28s %s" % [row["name"], row["hash_hex"]])


# --- the checks ---------------------------------------------------------------


func _check(check_name: String, passed: bool, detail: String) -> void:
	_checks.append({"name": check_name, "passed": passed, "detail": detail})


## Every case's verdict, and the fingerprint each `--break` mode has to leave.
##
## The house rule, and it is the one that cost six checks in `shell_probe.gd`: a
## check that cannot fail is not a check, and a negative control that accepts a
## pre-existing red proves nothing. So each mode below names the *measurement* it
## must have moved, and `--break` fails if that specific check is still green.
func _verdict() -> void:
	# The checks every driving case owes, whatever it is measuring.
	if not _results.is_empty() and _case != "place":
		var worst_input := 0.0
		var steer_missing := 0
		var steer_asked := 0
		for row in _results:
			worst_input = maxf(worst_input, float(row["input_error"]))
			if float(row["steer_commanded"]) > 0.0:
				steer_asked += 1
				# The rate limiter needs time to get there, so the bar is "most of the
				# way", not "exactly". A run where the solver never steered at all reads
				# as zero and is what this is looking for.
				if float(row["steer_reached"]) < float(row["steer_commanded"]) * 0.5:
					steer_missing += 1
		_check("scripted input reached the solver", worst_input < 1e-6,
			"worst |commanded - consumed| = %.6f over %d run(s)" % [worst_input, _results.size()])
		if steer_asked > 0:
			_check("commanded lock reached the steering", steer_missing == 0,
				"%d of %d runs never reached half the commanded lock" %
					[steer_missing, steer_asked])

		if _ground == "circuit":
			var tilt := 0.0
			for row in _results:
				tilt = maxf(tilt, float(row["normal_tilt_max_deg"]))
			# **The fixture check, and it is the only one every circuit case owes.** A
			# rig that reports a perfectly level contact normal everywhere on a circuit
			# with 8% crossfall and 4.6% grades is not driving the circuit — it is
			# driving a plane and writing "circuit" on the page. That is the whole of
			# what #240 is about, so it is asserted rather than assumed.
			#
			# **"a surface other than asphalt was touched" used to be here and was
			# wrong.** Once `--case=slope` and `--case=crossfall` were bounded so they
			# stop before the road does, they correctly touch nothing but asphalt, and a
			# generic check demanding otherwise turned two good cases red for doing
			# exactly what they were fixed to do. Which surface a case owes is the
			# case's own business, and it is asserted per case below.
			_check("contact normals are not level", tilt > 0.5,
				"worst contact-normal tilt %.2f deg (a plane gives 0.00)" % tilt)

	match _case:
		"calibrate":
			_check_calibrate()
		"place":
			_case_place()
		"section":
			_case_section()
		"curb":
			# **Only on the circuit.** A plane has no kerb, and the flat half of a
			# matched pair is the *control* — demanding it find a kerb is demanding the
			# control fail to be a control. Both of these checks did exactly that for one
			# full-suite run.
			if _ground == "circuit":
				var curb_ticks := 0
				for row in _results:
					curb_ticks += int((row["surface_ticks"] as Array)[1])
				_check("a wheel was on the kerb", curb_ticks > 0,
					"%d wheel-ticks on surface_type 1" % curb_ticks)
		"verge":
			if _ground == "circuit":
				var grass_ticks := 0
				for row in _results:
					grass_ticks += int((row["surface_ticks"] as Array)[2])
				_check("a wheel was on the verge", grass_ticks > 0,
					"%d wheel-ticks on surface_type 2" % grass_ticks)
		"zero":
			# **The #239 question, and the answer is a number rather than an opinion.**
			# Agent B measured the kart spinning to 178.778 deg on Valdirone with
			# `input.steer` bit-identical zero, and 0.079 deg on the proving ground with
			# the identical input. This case asks the same question at 55 stations and
			# reports the worst body slip reached *while the chassis was still inside the
			# white lines*.
			#
			# The threshold is `DEPARTURE_RAD`, the same 30 degrees `test_vehicle.cpp`,
			# `tire_probe.gd` and `drive_probe.gd` all mean by "no longer following the
			# road", so a red here is the same red they would print.
			var worst_on_road := 0.0
			var worst_at := ""
			var zero_load_ticks := 0
			var zero_load_rows := 0
			var on_road_rows := 0
			for row in _results:
				if float(row["slip_on_road"]) > worst_on_road:
					worst_on_road = float(row["slip_on_road"])
					worst_at = row["name"]
				if not bool(row["left_road"]):
					on_road_rows += 1
					if int(row["zero_load_ticks_with_contact"]) > 0:
						zero_load_rows += 1
						zero_load_ticks += int(row["zero_load_ticks_with_contact"])
			_check("no departure with zero input while on the road",
				worst_on_road < DEPARTURE_RAD,
				"worst on-road body slip %.2f deg at %s, over %d stations (departure is %.0f)" % [
					rad_to_deg(worst_on_road), worst_at, _results.size(),
					rad_to_deg(DEPARTURE_RAD)])
			# Not a pass/fail — a number the report needs. B found min wheel load
			# reaching exactly 0.0 N with all four corners still reporting contact two
			# ticks before the kart let go, and asked whether that state is cause or
			# symptom. It occurs on rows that never leave the road and never exceed a
			# degree of slip, so it is not sufficient to cause anything.
			print("    zero-load-with-four-contacts occurs on %d of the %d runs that never"
				% [zero_load_rows, on_road_rows])
			print("    left the road, %d tick(s) in total, none of which exceeded %.2f deg"
				% [zero_load_ticks, rad_to_deg(worst_on_road)])
			print("    of body slip. So the state is reachable without a departure: it is a")
			print("    symptom of the road's own relief, not a cause of anything.")
		"step":
			var vertical := 0.0
			for row in _results:
				vertical = maxf(vertical, float(row["peak_vertical_g"]))
			_check("the step was actually driven into", vertical > 1.0,
				"peak vertical %.2f g" % vertical)

	print("")
	var failed := 0
	for row in _checks:
		if not bool(row["passed"]):
			failed += 1
		print("    %-42s %-4s %s" % [row["name"], "ok" if row["passed"] else "FAIL", row["detail"]])
	print("    %d check(s), %d failed" % [_checks.size(), failed])

	if _break != "":
		# **Inverted, and it demands the saboteur's own fingerprint.** A `--break` run
		# that goes red for a reason the sabotage did not cause is not a caught
		# sabotage, it is a coincidence — `shell_probe.gd`'s first cut reported exactly
		# that. `_fingerprint` is set by `_break_fingerprint` below and has to name a
		# check that is now failing.
		var caught := _break_fingerprint()
		print("    break=%s: fingerprint '%s' %s" % [
			_break, _fingerprint, "present" if caught else "ABSENT"])
		if caught:
			print("    negative control CAUGHT")
			quit(0)
		else:
			printerr("    negative control NOT caught — the check it targets is still green")
			quit(1)
		return
	quit(1 if failed > 0 else 0)


## Which check each sabotage has to have taken red, by name.
func _break_fingerprint() -> bool:
	match _break:
		"input":
			_fingerprint = "scripted input reached the solver"
		"flatground":
			_fingerprint = "contact normals are not level"
		"nocurb":
			_fingerprint = "a wheel was on the kerb"
		"noterrain":
			_fingerprint = "the step was actually driven into"
		"calib":
			_fingerprint = "calibration reproduces drive.sh"
		_:
			_fingerprint = "(unknown mode)"
			return false
	for row in _checks:
		if String(row["name"]).begins_with(_fingerprint) and not bool(row["passed"]):
			return true
	return false


## The reference figures `--case=calibrate` has to reproduce.
##
## **They are read from the running `drive_probe.gd`, not restated here.**
## `terrain.sh` runs that probe and passes its numbers in with `--reference=`, so
## this file cannot hold a stale copy of a figure that moved — which is how
## `CLAUDE.md`'s own table came to say 139.8 km/h and 4.51 s when the build measures
## 140.1 and 4.38.
func _check_calibrate() -> void:
	var reference := Cmdline.as_string(_args, "reference", "")
	if reference == "":
		_check("calibration reproduces drive.sh", false,
			"no --reference=top,zero100 given; run through tools/verify/terrain.sh")
		return
	var parts := reference.split(",")
	if parts.size() < 2:
		_check("calibration reproduces drive.sh", false, "--reference needs top,zero100")
		return
	var want_top := float(parts[0])
	var want_100 := float(parts[1])

	var got_top := 0.0
	var got_100 := -1.0
	for row in _results:
		got_top = maxf(got_top, KartCore.ms_to_kmh(float(row["top_speed"])))
		if float(row["time_to_100"]) >= 0.0 and (got_100 < 0.0 or float(row["time_to_100"]) < got_100):
			got_100 = float(row["time_to_100"])
	var top_error: float = absf(got_top - want_top) / maxf(want_top, 1e-6)
	var s_error: float = absf(got_100 - want_100) / maxf(want_100, 1e-6)
	_check("calibration reproduces drive.sh",
		top_error <= CALIBRATE_TOLERANCE and s_error <= CALIBRATE_TOLERANCE,
		"top %.2f vs %.2f km/h (%.3f%%), 0-100 %.3f vs %.3f s (%.3f%%), tolerance %.1f%%" % [
			got_top, want_top, top_error * 100.0, got_100, want_100, s_error * 100.0,
			CALIBRATE_TOLERANCE * 100.0,
		])


# --- --case=place --------------------------------------------------------------


## The placement raycast against `KartTrack.grid_transform()`.
##
## Two independent answers to "where is the road at this station and this lateral":
## one from the physics server reading the built collider, one from
## `docs/TRACK_SCHEMA.md`'s cross-section formula. They have to agree, and if they
## do not, one of the two consumers has drifted — which is the same class of
## disagreement `circuit.sh --case=agree` measures between the collider and the
## mesh.
##
## The tolerance is 6 mm and it is **derived**: the collider is triangulated at
## `surface_meshes()`'s default 0.02 m sagitta, so a ray landing mid-chord reads up
## to one sagitta low, and the grid slots sit at |lateral| = 3.0 m where the
## cross-section is piecewise linear and reproduced exactly. 6 mm is three sagittas.
const PLACE_TOLERANCE := 0.006


func _case_place() -> void:
	if not _checks.is_empty():
		return
	print("")
	print("    slot   station  lateral   ray y      formula y    dy mm    dup deg   body")
	var worst := 0.0
	var worst_angle := 0.0
	for slot in _track.grid_count():
		var formula := _track.grid_transform(slot, SPAWN_LIFT)
		# The slot's own station and lateral are not published, so the formula's own
		# origin is projected back to get them. That is the honest direction: it uses
		# `project`, which is a third code path, rather than re-reading `track.json`.
		var found := _track.project(formula.origin)
		var placed := _place(float(found["distance"]), float(found["lateral"]))
		if placed.is_empty():
			_check("placement agrees with grid_transform", false,
				"no collider under grid slot %d" % (slot + 1))
			return
		var ray: Transform3D = placed["transform"]
		var dy := (ray.origin.y - formula.origin.y) * 1000.0
		var dup := rad_to_deg(ray.basis.y.angle_to(formula.basis.y))
		worst = maxf(worst, absf(dy))
		worst_angle = maxf(worst_angle, dup)
		print("    %4d %9.2f %8.2f %9.4f %12.4f %8.2f %10.3f   %s" % [
			slot + 1, float(found["distance"]), float(found["lateral"]),
			ray.origin.y, formula.origin.y, dy, dup, placed["body"],
		])
	_check("placement agrees with grid_transform",
		worst <= PLACE_TOLERANCE * 1000.0,
		"worst |dy| %.2f mm (tolerance %.1f mm), worst up-axis %.3f deg" % [
			worst, PLACE_TOLERANCE * 1000.0, worst_angle,
		])


# --- --case=section ------------------------------------------------------------


## What the collision **is**, across the road at one station, in 0.1 m steps.
##
## A diagnostic rather than a gate, and it earned its place: `--case=curb`'s
## run-wide row reported zero kerb wheel-ticks and 64.8% grass at a station whose
## `track.json` entry declares a kerb from 367.460 to 402.460 m. Reading the
## cross-section is the difference between "the kerb collider is missing" and "the
## scenario missed the kerb", and no other output in this file can tell those apart.
func _case_section() -> void:
	var station := Cmdline.as_float(_args, "station", 380.0)
	var frame := _track.sample(station)
	var half := float(frame["width"]) * 0.5
	var span := Cmdline.as_float(_args, "span", half + 6.0)
	print("")
	print("    station %.1f, width %.2f m, half %.3f m" % [station, float(frame["width"]), half])
	print("    lateral      y       surface  body")
	var lateral := -span
	var last := ""
	while lateral <= span:
		var found := _place(station, lateral)
		var label := "(nothing)" if found.is_empty() else "%d %s" % [
			int(found["surface"]), found["body"]]
		# Only the transitions, and the first and last row. A 24 m section at 0.1 m is
		# 240 rows of which six are interesting.
		if label != last:
			if found.is_empty():
				print("    %8.2f        -   %s" % [lateral, label])
			else:
				print("    %8.2f %8.3f   %s" % [lateral, (found["point"] as Vector3).y, label])
			last = label
		lateral += 0.1
	print("    (only transitions are printed; lateral is positive to the right of travel)")


# --- --case=step ---------------------------------------------------------------


## Place from a world XZ rather than from a station: the terrain's worst step is a
## property of the height field, not of the lap, and there is no station that names
## it.
func _place_world(at: Vector3, toward: Vector3) -> Dictionary:
	var space := _world.get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(
		Vector3(at.x, at.y + PLACE_RAY_LIFT, at.z),
		Vector3(at.x, at.y + PLACE_RAY_LIFT - PLACE_RAY_LENGTH, at.z))
	var hit := space.intersect_ray(query)
	if hit.is_empty():
		return {}
	var normal: Vector3 = hit["normal"]
	if normal.y <= 0.0:
		return {}
	var point: Vector3 = hit["position"]
	var flat := Vector3(toward.x - at.x, 0.0, toward.z - at.z)
	if flat.length_squared() < 1e-6:
		flat = Vector3(0.0, 0.0, -1.0)
	var forward := flat.normalized()
	var back := -(forward - normal * forward.dot(normal)).normalized()
	var side := normal.cross(back).normalized()
	var collider: Object = hit["collider"]
	return {
		"transform": Transform3D(Basis(side, normal, back), point + normal * SPAWN_LIFT),
		"normal": normal, "point": point,
		"surface": int(collider.get_meta("surface_type", 0)) if collider != null else 0,
		"body": String(collider.name) if collider != null else "",
		"grade": 0.0, "bank_pct": 0.0, "crown_pct": 0.0, "width": 0.0, "curvature": 0.0,
	}


## The height field's worst cell-to-cell step, and the two cells it is between.
##
## `TrackTerrain`'s own docstring measures 2.382 m at four smoothing passes and
## says why it cannot be smoothed away: it is between two *pinned* vertices where
## the lap passes close to itself at two different heights. This walks the same
## field rather than trusting that number — `--case=step` prints what it found, so
## a field that changed is visible rather than silently mis-targeted. Sampled
## through the public `height_at`, on the lattice recomputed in `_build_circuit`.
func _worst_terrain_step() -> Dictionary:
	if _terrain == null or _terrain_cells <= 0:
		return {}
	var best := 0.0
	var at := Vector3.ZERO
	var to := Vector3.ZERO
	var cell := _terrain_cell
	# **On the vertices, not on the cell centers.** `height_at` interpolates inside a
	# cell, so a sample at a center returns the mean of its corners and a step
	# measured between two centers is the mean of two vertex steps — the first cut
	# did that and reported 2.254 m where `TrackTerrain`'s own docstring measures
	# 2.382. Sampling at `origin + index * cell` hits `u = v = 0` and returns the
	# vertex exactly. The last row and column are skipped because `height_at` clamps
	# its lookup to the final *cell* and a sample on the far edge folds back.
	for row in _terrain_cells:
		for column in _terrain_cells:
			var x := _terrain_origin.x + float(column) * cell
			var z := _terrain_origin.y + float(row) * cell
			var here := _terrain.height_at(x, z)
			var east := _terrain.height_at(x + cell, z)
			var south := _terrain.height_at(x, z + cell)
			if column + 1 < _terrain_cells and absf(east - here) > best:
				best = absf(east - here)
				at = Vector3(x, here, z)
				to = Vector3(x + cell, east, z)
			if row + 1 < _terrain_cells and absf(south - here) > best:
				best = absf(south - here)
				at = Vector3(x, here, z)
				to = Vector3(x, south, z + cell)
	if best <= 0.0:
		return {}
	# Start on the high side, a couple of cells back, so the kart arrives at the step
	# with the run-up its speed implies rather than starting on top of it.
	var high := at if at.y >= to.y else to
	var low := to if at.y >= to.y else at
	var run := (high - low)
	run.y = 0.0
	if run.length() > 0.01:
		high = high + run.normalized() * cell * 2.0
	print("    terrain worst step %.3f m between %s and %s (cell %.1f m)" % [best, high, low, cell])
	return {"step": best, "from": high, "to": low}


# --- --case=survey -------------------------------------------------------------


## The geometry the case stations were chosen off, printed so the choices are
## checkable rather than asserted. No world, no kart, no physics.
func _survey() -> void:
	var m := _track.measurements()
	print("Valdirone Nuova, layout %s" % _track.layout())
	print("    length              %10.3f m" % float(m["length"]))
	print("    longest straight    %10.3f m at %.1f" % [
		float(m["longest_straight"]), float(m["longest_straight_at"])])
	print("    elevation range     %10.3f m  (low %.3f at %.1f, high %.3f at %.1f)" % [
		float(m["elevation_range"]), float(m["elevation_low"]), float(m["elevation_low_at"]),
		float(m["elevation_high"]), float(m["elevation_high_at"])])
	print("    width               %10.3f - %.3f m" % [float(m["min_width"]), float(m["max_width"])])
	print("    worst ground slope  %10.3f %%  between stations %s" % [
		float(m["worst_ground_slope_pct"]), m["worst_ground_slope_at"]])

	var steepest := 0.0
	var steep_at := 0.0
	var bankiest := 0.0
	var bank_at := 0.0
	var crowniest := 0.0
	var straight_bank := 0.0
	var straight_bank_at := 0.0
	var level := 0
	var station := 0.0
	while station < _track.length():
		var s := _track.sample(station)
		# `grade` is a **fraction** and `bank_pct` is a **percent** — two fields of one
		# Dictionary in two units. Multiplied here so the table is one unit. Reading
		# `grade` as a percent is a factor-of-100 error that looks like a flat circuit.
		var grade: float = absf(float(s["grade"])) * 100.0
		var bank: float = absf(float(s["bank_pct"]))
		var crown: float = absf(float(s["crown_pct"]))
		var straight := absf(float(s["curvature"])) < 1e-9
		if grade > steepest:
			steepest = grade
			steep_at = station
		if bank > bankiest:
			bankiest = bank
			bank_at = station
		crowniest = maxf(crowniest, crown)
		if straight and bank > straight_bank:
			straight_bank = bank
			straight_bank_at = station
		# **The flat-plane question, asked of the circuit.** A station is "level" only
		# if it is straight, ungraded, unbanked *and uncrowned* — which is the surface
		# every rig in `tests/core/` synthesizes. The crown is the term that is easy to
		# forget and it is on every meter of asphalt here.
		if straight and grade < 0.01 and bank < 0.01 and crown < 0.01:
			level += 1
		station += 1.0
	print("    steepest road grade %10.3f %%  at station %.1f" % [steepest, steep_at])
	print("    max crossfall       %10.3f %%  at station %.1f" % [bankiest, bank_at])
	print("    max crossfall on a straight %2.3f %%  at station %.1f" % [
		straight_bank, straight_bank_at])
	print("    max crown           %10.3f %%" % crowniest)
	print("    stations that are straight, ungraded, unbanked AND uncrowned: %d of %d" % [
		level, int(_track.length())])
	print("")
	print("    station  grade%  bank%  crown%  width  curvature  elevation")
	station = 0.0
	while station < _track.length():
		var s := _track.sample(station)
		print("    %7.1f %7.3f %6.2f %7.2f %6.2f %10.5f %10.3f" % [
			station, float(s["grade"]) * 100.0, float(s["bank_pct"]), float(s["crown_pct"]),
			float(s["width"]), float(s["curvature"]), float(s["elevation"]),
		])
		station += 25.0
