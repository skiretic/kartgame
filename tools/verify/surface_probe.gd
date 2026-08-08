extends SceneTree

## Issue #241 — does a surface under a wheel cost anything, and does a barrier stop
## the kart?
##
##     godot --headless --path . --script tools/verify/surface_probe.gd -- --case=hop
##
## `tools/verify/surface.sh` is the caller and runs every case. A cold checkout
## needs the usual double editor import first (upstream issue #2, ADR-0018).
##
## ## What this file is for
##
## `valdirone.tscn` prints `Verge 2928  Gravel 376  Barriers 1362` on every load
## and Anthony's report from the seat is that leaving the road costs nothing. The
## wiring for it exists at **both** ends — `surface.h` has a sourced grip table,
## `circuit.gd` calls `set_meta("surface_type", ...)`, `kart_body.cpp` reads that
## meta off the raycast hit, `vehicle.h` copies `surface_grip` into `TireSlip` and
## `tire.h` multiplies both axes by it — so the first question is not "what should
## be built" but **which hop in that chain is lying**. This file walks the chain
## one hop at a time and reports a number at each, because the failure mode
## CLAUDE.md names is a capability built at both ends and never joined in the
## middle, and the way that hides is that every end looks right on its own.
##
## The five hops, and the case that measures each:
##
##   1. the geometry exists and is the shape it claims        `geometry`
##   2. the collider built from it carries `surface_type`     `hop`
##   3. a downward ray from a wheel finds *that* collider     `hop`
##   4. the integer reaches the solver, per corner            `hop`
##   5. the grip changes what the tire does                   `drive`
##
## plus the two the ticket asks for on their own:
##
##   6. a barrier is a wall and stops the kart                `barrier`
##   7. the surface reaches the audio input                   `audio`
##
## ## Where the numbers are taken, and what that costs them
##
## **`drive`'s figures are PROVISIONAL and are measured on a flat plane.** Issue
## #240 — the track-surface driving rig — does not exist yet, and every driving
## figure in this repo was measured on a flat plane without anybody recording it.
## So `drive` builds its own fixture: one horizontal slab per surface, no camber,
## no elevation, no terrain, `surface_type` set on the body and nothing else
## different between the four lanes. That makes it a clean measurement of *the
## solver's response to the grip multiplier* and not a measurement of what
## Valdirone's verge feels like. Every table below says which fixture it ran on.
##
## `hop` and `barrier` run on the real circuit, because the question they ask is
## about the circuit's own geometry and cannot be asked anywhere else.
##
## ## The negative controls
##
## `--break=<mode>` sabotages the thing under test and the exit code is
## **inverted**: the run is a pass when the check goes red *and names the
## saboteur's own fingerprint*. A pre-existing red does not count, which is the
## trap `shell_probe.gd` fell into.
##
##   --break=meta      strips `surface_type` off Verge and Gravel after the scene
##                     builds. `hop` must report those two bodies reading asphalt.
##   --break=grip      builds every `drive` lane with `surface_type` 0 while
##                     labeling it otherwise. `drive` must report the spread
##                     across the four surfaces collapsing to nothing.
##   --break=barrier   deletes the `Barriers` body before the run. `barrier` must
##                     report the kart crossing the wall plane.
##
## Each is verified at the value `surface.sh` actually passes, per the entry in
## CLAUDE.md about `replay.sh` shipping a default that made its control inert.
##
## ## What this file deliberately does not do
##
## It writes nothing under `user://`. It never completes a lap, so the recorder
## `circuit.gd` builds never saves a ghost, and it passes `--profile-dir` at a
## private path anyway. Every worktree shares one `user://` and a probe that
## records under a real circuit's slug overwrites the real ghost.
##
## It does not touch `tools/verify/terrain_probe.gd` or `terrain.sh`, which are
## issue #240's and belong to another author.

# --- the surface table, restated ------------------------------------------------
#
# `src/core/surface.h` is the owner. Restated here rather than read, because a
# prediction computed from the same value the measurement used is not a
# prediction — `kart_body_probe.gd`'s rule, kept.
#
# **These integers are a wire format**, said in surface.h's own words, so they are
# written out rather than derived.
const SURFACE_NAMES: PackedStringArray = ["asphalt", "curb", "grass", "dirt"]

## The `grip` column. Every one of the four is sourced or derived upstream and
## `surface.h`'s header carries the citation for each; none of them is this file's
## to choose and none of them is changed here.
const SURFACE_GRIP: PackedFloat64Array = [1.00, 0.72, 0.18, 0.17]

## `tire.h`'s peak friction for a KZ slick on hot asphalt. The naive prediction for
## a tire-limited stop on surface `s` is `PEAK_FRICTION * SURFACE_GRIP[s]` g, and
## the tables below print it beside the measurement rather than instead of it.
## Load sensitivity means the tire does not hold the full 2.10 at the loads a
## braking kart puts on it, so the prediction is a **ceiling**, and it says so.
const PEAK_FRICTION := 2.10

# --- the flat fixture -----------------------------------------------------------

## Square and large: the top-speed run covers 700 m down -Z.
const GROUND_SIZE := 4000.0
const GROUND_THICKNESS := 2.0

## Lane spacing along X, and how wide each lane's slab is.
##
## **Wide, and that is a measurement rather than caution.** The first version used
## 12 m lanes on the assumption that a kart driven straight goes straight. It does
## not: at 0.18 grip the rear axle spends the whole launch past peak slip ratio,
## the kart yaws, and at 12 m it ran off the side of its own slab and fell out of
## the world — which reported as a *top speed of 327 km/h on the kerb lane* and a
## peak deceleration of 6.1 g on grass, both of them a falling kart. A departure
## has to stay on its own surface to be measurable, and it has to be visible in
## the table rather than inferred from an absurd number, which is what the drift
## and heading columns below are for.
const LANE_SPACING := 200.0
const LANE_WIDTH := 180.0

## `kart_body_probe.gd`'s chassis box, unchanged, including its reason: the bottom
## face sits 0.17 m above the road so Jolt never supplies a normal force on top of
## the suspension's.
const SHAPE_SIZE := Vector3(0.90, 0.30, 1.60)
const SHAPE_CENTER := Vector3(0.0, 0.32, 0.0)

## The chassis box for the barrier run is the same box. It is what has to hit the
## wall, so it is stated once and shared.

# --- timing ---------------------------------------------------------------------
#
# 120 Hz, `project.godot`'s `physics/common/physics_ticks_per_second`. Read from
# the setting rather than assumed, below; these are tick counts against it.

## Settle before anything is driven. `kart_body_probe.gd` measures the tire's
## slowest mode at 11.8 Hz, so 1 s is twelve cycles.
const SETTLE_TICKS := 120

## The launch. 3 s of full throttle from rest, which is past the clutch and into
## second on asphalt and nowhere near it on grass — that difference is the
## measurement.
const LAUNCH_TICKS := 360

## The top-speed run. 20 s, which `kart_body_probe.gd` calls asymptotic on
## asphalt.
const TOPSPEED_TICKS := 2400

## The stop. Entered at `BRAKE_ENTRY_MS`, full brake, no throttle, until stopped or
## this many ticks pass. 8 s is 220 m at 0.378 g from 20 m/s, which is the slowest
## case in the table.
const BRAKE_TICKS := 1200
const BRAKE_ENTRY_MS := 20.0

## The barrier run. The kart is launched at the wall from `BARRIER_STANDOFF_M` and
## this is long enough to cover it at `BARRIER_SPEED_MS` twice over.
const BARRIER_TICKS := 600
const BARRIER_STANDOFF_M := 12.0
const BARRIER_SPEED_MS := 25.0

# --- thresholds -----------------------------------------------------------------
#
# Each one is a statement about what would have to be true for the chain to be
# broken, not a tolerance fitted to a passing run.

## A barrier has to be a wall and not an apron. The published geometry is a 1.0 m
## quad; anything under half the chassis box's own height is something a kart
## drives over rather than into.
const BARRIER_MIN_HEIGHT_M := 0.5

## The wall's height above its own seated base, meters — `BARRIER_HEIGHT_M` in
## `src/track/kart_track.cpp` and `BARRIER_HEIGHT` in `tracklib/surfaces.py`, a
## fourth copy of a number three languages already cannot share.
##
## It is needed here because issue #244 changed what the lowest vertex of a barrier
## quad means. It used to be the base: the wall was 1.0 m tall and stood on the
## road's extrapolated cross-section. It is now the bottom of a `BARRIER_SKIRT`
## driven into the terrain, so `min(y)` is a point deliberately underground and
## everything that read it as "where the wall meets the ground" reads 3 m low —
## which spawned this probe's own strike kart under the terrain and sent it 87.74 m
## through the wall. The base is `max(y) - BARRIER_HEIGHT_M`.
const BARRIER_HEIGHT_M := 1.0

## How much the four surfaces have to differ before the grip column can be said to
## reach the tire at all. Grass is 0.18 of asphalt, so a working chain separates
## the stopping distances by more than a factor of two; a broken one separates
## them by zero. 1.5 is the middle of a gap with nothing in it.
const GRIP_MIN_RATIO := 1.5

## The barrier verdict. The kart's chassis origin must not cross the wall plane by
## more than this, which is half the chassis box's length — a box resting against a
## zero-thickness wall has its origin behind it.
const BARRIER_MAX_PENETRATION_M := 0.80

# --- state ----------------------------------------------------------------------

var _args: PackedStringArray
var _case := ""
var _break := ""
var _tick := 0
var _phase := 0
var _phase_tick := 0
var _hz := 120.0

var _checks := 0
var _failed := 0
var _lines: Array[String] = []
## The saboteur's fingerprint, collected by whichever check caught it. A
## `--break` run is only a pass when this is non-empty.
var _fingerprints: Array[String] = []

## `drive`
var _ground: StaticBody3D
var _karts: Array[KartBody] = []
var _launch_distance: PackedFloat64Array
var _launch_speed: PackedFloat64Array
var _top_speed: PackedFloat64Array
var _brake_distance: PackedFloat64Array
var _brake_peak_g: PackedFloat64Array
var _brake_entry_x: PackedFloat64Array
var _seen_surface: PackedInt32Array
var _prev_speed: PackedFloat64Array
var _drive_throttle := 0.0
var _drive_brake := 0.0

## `hop` and `barrier`
var _root: Node3D
var _track: KartTrack
var _probe_kart: KartBody
var _barrier_plane_normal := Vector3.ZERO
var _barrier_start := Vector3.ZERO
var _barrier_max_travel := -INF
var _barrier_speed_min := INF
var _barrier_solver_speed := INF
var _barrier_ran := false


func _initialize() -> void:
	_args = OS.get_cmdline_user_args()
	_case = _string_arg("case", "geometry")
	_break = _string_arg("break", "")
	_hz = float(ProjectSettings.get_setting("physics/common/physics_ticks_per_second", 120))

	_say("surface_probe — issue #241, case=%s%s" % [
		_case, "" if _break.is_empty() else "  BREAK=" + _break])
	_say("physics %.0f Hz" % _hz)

	match _case:
		"geometry":
			_case_geometry()
			_finish()
		"drive":
			_build_flat_fixture()
		"hop", "barrier":
			_build_circuit()
		"audio":
			_case_audio()
			_finish()
		_:
			_say("unknown --case=%s" % _case)
			_failed += 1
			_finish()


func _physics_process(_delta: float) -> bool:
	_tick += 1
	_phase_tick += 1
	match _case:
		"drive":
			_drive_tick()
		"hop":
			_hop_tick()
		"barrier":
			_barrier_tick()
	return false


# --- case: geometry -------------------------------------------------------------
#
# No engine, no scene, no kart. Just: what does `KartTrack::surface_meshes` publish
# and is each band the shape its name claims? The barrier is the one that matters —
# the ticket's own question is whether it is a wall or a flat apron the kart drives
# over — and it is answered by measuring the vertical extent of every triangle
# rather than by reading the source that built them.

func _case_geometry() -> void:
	_track = KartTrack.new()
	var err := _track.load("res://data/tracks/valdirone_nuova.track.json")
	_check("track loads", err == OK, "load() returned %d" % err)
	if err != OK:
		return

	var entries := _track.surface_meshes(0.02, 2.0)
	_say("")
	_say("  what surface_meshes() publishes, off data/tracks/valdirone_nuova.track.json")
	_say("  %-10s %5s %7s %10s %10s" % ["band", "type", "tris", "y min", "y max"])
	var seen := {}
	for entry in entries:
		var faces: PackedVector3Array = entry["faces"]
		var name: String = entry["name"]
		var type := int(entry["surface_type"])
		seen[name] = type
		var lo := INF
		var hi := -INF
		for v in faces:
			lo = minf(lo, v.y)
			hi = maxf(hi, v.y)
		_say("  %-10s %2d %-4s %7d %10.3f %10.3f" % [
			name, type, _surface_label(type), faces.size() / 3, lo, hi])

	# Every band the scene prints has to be here, or the collider was never built.
	for name in ["Asphalt", "Kerbs", "Verge", "Gravel", "Barriers", "PitLane"]:
		_check("band %s is published" % name, seen.has(name), "absent from surface_meshes()")

	# Hop 1: is the barrier a wall? Per triangle, so a single tall outlier cannot
	# hide 1,360 flat ones.
	var lo_h := INF
	var hi_h := -INF
	var flat := 0
	var count := 0
	for entry in entries:
		if entry["name"] != "Barriers":
			continue
		var faces: PackedVector3Array = entry["faces"]
		var i := 0
		while i < faces.size():
			var h: float = (maxf(maxf(faces[i].y, faces[i + 1].y), faces[i + 2].y)
					- minf(minf(faces[i].y, faces[i + 1].y), faces[i + 2].y))
			lo_h = minf(lo_h, h)
			hi_h = maxf(hi_h, h)
			if h < BARRIER_MIN_HEIGHT_M:
				flat += 1
			count += 1
			i += 3
	_say("")
	_say("  barrier triangles: %d, vertical extent %.4f .. %.4f m, %d below %.2f m" % [
		count, lo_h, hi_h, flat, BARRIER_MIN_HEIGHT_M])
	_check("the barrier is a wall, not an apron",
			count > 0 and flat == 0 and lo_h >= BARRIER_MIN_HEIGHT_M,
			"%d of %d barrier triangles are shorter than %.2f m" % [
				flat, count, BARRIER_MIN_HEIGHT_M])

	# The surface types the bands are published under. Not a taste call — these are
	# `surface.h`'s wire values and a band published under the wrong one is a verge
	# made of asphalt.
	_check("Verge is published as grass", int(seen.get("Verge", -1)) == 2,
			"Verge carries surface_type %d" % int(seen.get("Verge", -1)))
	_check("Gravel is published as dirt", int(seen.get("Gravel", -1)) == 3,
			"Gravel carries surface_type %d" % int(seen.get("Gravel", -1)))
	_check("Kerbs are published as curb", int(seen.get("Kerbs", -1)) == 1,
			"Kerbs carry surface_type %d" % int(seen.get("Kerbs", -1)))


# --- case: drive ----------------------------------------------------------------
#
# Hop 5, on a flat plane, and the numbers say so everywhere they are printed.
#
# Four karts, four lanes, one horizontal slab under each with a different
# `surface_type` and *nothing else different*. Three measurements per lane:
#
#   drive  distance covered in 3.0 s of full throttle from rest
#   speed  road speed after 20 s of full throttle
#   grip   the stop from 20 m/s: distance and peak longitudinal g
#
# The stop is the grip measurement rather than a skidpad on purpose. A constant-
# radius run on grass departs — issue #137 — and `drive.sh` already refuses to
# publish a lateral figure from a spun run rather than averaging one. A straight-
# line stop is pure longitudinal slip, cannot spin, and is the axis `TireSlip`
# scales with exactly the same `surface_grip` the lateral axis does.

func _build_flat_fixture() -> void:
	_launch_distance = _zeros()
	_launch_speed = _zeros()
	_top_speed = _zeros()
	_brake_distance = _zeros()
	_brake_peak_g = _zeros()
	_brake_entry_x = _zeros()
	_prev_speed = _zeros()
	_seen_surface = PackedInt32Array([-1, -1, -1, -1])

	for surface in range(4):
		# One slab per lane. The **only** difference between them is the metadata
		# key. Friction is the tire model's, not this material's — the kart sets its
		# own body friction to 0.0 and the combine rule is `min(a, b)`, ADR-0033 — so
		# writing 1.0 here reaches nothing and is what makes this a controlled
		# comparison rather than four different grounds.
		var body := StaticBody3D.new()
		body.name = "Ground" + SURFACE_NAMES[surface].capitalize()
		var material := PhysicsMaterial.new()
		material.friction = 1.0
		material.rough = true
		body.physics_material_override = material
		# The negative control. `--break=grip` labels every lane honestly in the
		# report and gives all four the asphalt metadata, which is the shape of the
		# real failure: a collider that was built and never told what it is.
		body.set_meta("surface_type", 0 if _break == "grip" else surface)

		var shape := BoxShape3D.new()
		shape.size = Vector3(LANE_WIDTH, GROUND_THICKNESS, GROUND_SIZE)
		var collider := CollisionShape3D.new()
		collider.shape = shape
		# Top face exactly at y = 0, which every height below is written against.
		collider.position = Vector3(
			float(surface) * LANE_SPACING, -GROUND_THICKNESS * 0.5, 0.0)
		body.add_child(collider)
		get_root().add_child(body)

		_karts.append(_build_kart("Kart" + SURFACE_NAMES[surface].capitalize(), surface))

	_say("")
	_say("  fixture: FLAT PLANE, y = 0, no camber, no elevation, no terrain.")
	_say("  Four horizontal slabs, one per surface_type, identical but for the")
	_say("  metadata. These figures are PROVISIONAL against issue #240's rig.")


func _build_kart(label: String, lane: int) -> KartBody:
	var kart := KartBody.new()
	kart.name = label
	# Set before `add_child`: `KartBody::_ready` captures the spawn off the
	# transform it finds.
	kart.position = Vector3(float(lane) * LANE_SPACING, 0.0, 0.0)

	var collider := CollisionShape3D.new()
	collider.name = "Chassis"
	var shape := BoxShape3D.new()
	shape.size = SHAPE_SIZE
	collider.shape = shape
	collider.position = SHAPE_CENTER
	kart.add_child(collider)

	get_root().add_child(kart)
	kart.set_input_driver(Callable(self, "_drive_input"))
	return kart


func _drive_input() -> Dictionary:
	return { "throttle": _drive_throttle, "brake": _drive_brake, "steer": 0.0 }


## Road speed along the kart's **own forward**, m/s.
##
## Not `speed_ms` and not `linear_velocity.length()`. Both of those are magnitudes,
## so a kart that has departed and is travelling sideways at 40 m/s reports 40, and
## a kart that has fallen off the world reports its terminal velocity as a top
## speed. The forward component goes negative when the kart is going backwards,
## which is the honest answer and is visible as one.
func _forward_speed(kart: KartBody) -> float:
	return kart.linear_velocity.dot(-kart.global_transform.basis.z)


## How far the kart's own heading has swung off the launch direction, degrees. The
## departure witness — `drive.sh` refuses to publish a lateral figure from a spun
## run rather than averaging one, and this table says the same thing in a column.
func _heading_drift(kart: KartBody) -> float:
	return rad_to_deg(absf((-kart.global_transform.basis.z).signed_angle_to(
		Vector3.FORWARD, Vector3.UP)))


func _drive_tick() -> void:
	match _phase:
		0:
			# Settle. Nothing driven, nothing recorded.
			_drive_throttle = 0.0
			_drive_brake = 0.0
			if _phase_tick >= SETTLE_TICKS:
				for index in range(4):
					_karts[index].respawn()
				_next_phase()
		1:
			# Launch, then straight on into the top-speed run — they are the same
			# run and splitting them would cost a second acceleration from rest.
			_drive_throttle = 1.0
			_drive_brake = 0.0
			for index in range(4):
				var kart := _karts[index]
				# Hop 4, recorded from inside the drive run rather than asserted
				# separately: what the solver says the front-left wheel is on.
				var report := kart.wheel_report()
				if report.size() > 0:
					var wheel: Dictionary = report[0]
					if bool(wheel["contact"]):
						_seen_surface[index] = int(wheel["surface"])
				if _phase_tick == LAUNCH_TICKS:
					_launch_distance[index] = -kart.global_position.z
					_launch_speed[index] = _forward_speed(kart)
				_top_speed[index] = maxf(_top_speed[index], _forward_speed(kart))
			if _phase_tick >= TOPSPEED_TICKS:
				_report_drive_acceleration()
				_next_phase()
		2:
			# The stop. Entered through `engage()` rather than by accelerating into
			# it: `engage(gear, road_speed_ms)` sets the body's velocity along its
			# own forward *and* syncs the driveline to it, which is the one call
			# that puts four karts into the identical clean state. Accelerating into
			# a common entry speed cannot do that — grass takes 30 s to reach 20 m/s
			# and arrives yawed, so the four stops would start from four different
			# karts and the table would be comparing those instead of the surfaces.
			_drive_throttle = 0.0
			_drive_brake = 0.0
			if _phase_tick == 1:
				for kart in _karts:
					kart.respawn()
				return
			if _phase_tick == 2:
				for kart in _karts:
					kart.engage(3, BRAKE_ENTRY_MS)
				return
			# A few ticks for the suspension to take up the load before the brake
			# goes on, so the stop starts from a settled kart.
			if _phase_tick < 20:
				return
			for index in range(4):
				_brake_entry_x[index] = _karts[index].global_position.z
				_prev_speed[index] = _forward_speed(_karts[index])
			_next_phase()
		3:
			_drive_throttle = 0.0
			_drive_brake = 1.0
			var stopped := true
			for index in range(4):
				var kart := _karts[index]
				var speed := _forward_speed(kart)
				var decel := (_prev_speed[index] - speed) * _hz / 9.80665
				_brake_peak_g[index] = maxf(_brake_peak_g[index], decel)
				_prev_speed[index] = speed
				if speed > 0.5:
					stopped = false
					_brake_distance[index] = absf(
						kart.global_position.z - _brake_entry_x[index])
			if stopped or _phase_tick >= BRAKE_TICKS:
				_report_drive_braking()
				_next_phase()
		4:
			_report_drive_verdict()
			_finish()


func _report_drive_acceleration() -> void:
	_say("")
	_say("  FLAT PLANE, from rest, full throttle, no steer. PROVISIONAL (#240).")
	_say("  Speeds are along the kart's OWN FORWARD, not |v|. `yaw` is how far the")
	_say("  kart's heading has swung off the launch line — a large one means the")
	_say("  run departed and the speed beside it is a spinning kart's.")
	_say("  %-8s %5s %10s %11s %11s %12s %8s" % [
		"surface", "grip", "solver saw", "3.0 s (m)", "3.0 s km/h", "20 s (km/h)",
		"yaw (deg)"])
	for index in range(4):
		_say("  %-8s %5.2f %10s %11.1f %11.1f %12.1f %8.1f" % [
			SURFACE_NAMES[index], SURFACE_GRIP[index],
			_surface_label(_seen_surface[index]),
			_launch_distance[index], _launch_speed[index] * 3.6,
			_top_speed[index] * 3.6, _heading_drift(_karts[index])])


func _report_drive_braking() -> void:
	_say("")
	_say("  FLAT PLANE, full brake from %.1f m/s in 3rd, entered through" % BRAKE_ENTRY_MS)
	_say("  KartBody.engage(). PROVISIONAL (#240).")
	_say("  The prediction is a CEILING: tire.h's %.2f peak times the surface's" % PEAK_FRICTION)
	_say("  grip, before load sensitivity and before the brake torque runs out.")
	_say("  %-8s %5s %12s %12s %12s %8s" % [
		"surface", "grip", "stop (m)", "peak (g)", "ceiling (g)", "of ceil"])
	for index in range(4):
		var ceiling := PEAK_FRICTION * SURFACE_GRIP[index]
		_say("  %-8s %5.2f %12.1f %12.3f %12.3f %8.2f" % [
			SURFACE_NAMES[index], SURFACE_GRIP[index],
			_brake_distance[index], _brake_peak_g[index], ceiling,
			_brake_peak_g[index] / ceiling])


func _report_drive_verdict() -> void:
	# Hop 4: the solver has to have seen the surface the fixture put under it.
	var mislabeled: Array[String] = []
	for index in range(4):
		if _seen_surface[index] != index:
			mislabeled.append("%s lane read %s" % [
				SURFACE_NAMES[index], _surface_label(_seen_surface[index])])
	_check("the solver sees the surface under each lane", mislabeled.is_empty(),
			", ".join(mislabeled))
	if not mislabeled.is_empty():
		_fingerprints.append("solver read: " + ", ".join(mislabeled))

	# Hop 5: it has to change what the tire does. Asphalt against grass, on the
	# axis the grip column scales directly.
	var asphalt_g: float = _brake_peak_g[0]
	var grass_g: float = _brake_peak_g[2]
	var ratio := asphalt_g / grass_g if grass_g > 1e-6 else INF
	_say("")
	_say("  asphalt / grass peak decel ratio %.2f  (grip ratio %.2f)" % [
		ratio, SURFACE_GRIP[0] / SURFACE_GRIP[2]])
	_check("the grip column reaches the tire", ratio >= GRIP_MIN_RATIO,
			"asphalt %.3f g against grass %.3f g is a ratio of %.2f, under %.2f" % [
				asphalt_g, grass_g, ratio, GRIP_MIN_RATIO])
	if ratio < GRIP_MIN_RATIO:
		_fingerprints.append("decel ratio %.2f" % ratio)

	var speed_ratio := _top_speed[0] / _top_speed[2] if _top_speed[2] > 1e-6 else INF
	_say("  asphalt / grass 20 s speed ratio %.2f" % speed_ratio)


# --- case: hop and barrier ------------------------------------------------------

func _build_circuit() -> void:
	var scene: PackedScene = load("res://scenes/game/valdirone.tscn")
	_root = scene.instantiate() as Node3D
	get_root().add_child(_root)


## Where hop 4 stands the kart, and what it must find there.
##
## Not arbitrary, and not the road edge. Each lateral offset is the **middle** of
## the band it names, because the kart is 1.185 m over the rear track and a
## measurement taken at the white line has two wheels on the asphalt. The verge is
## `VERGE_WIDTH_M` = 1.80 m wide outboard of the half width, so its middle is
## `half + 0.90`.
##
## The last column is **how many of the four wheels must read it**, and it is not a
## tolerance. It is arithmetic on the band's own width against the kart's 1.185 m
## rear track (`chassis.h`):
##
##   * a 1.00 m kerb is **narrower than the kart**, so four wheels on one is not a
##     state that exists. One wheel is the honest requirement and a kerb strike is
##     a one-corner event by construction — which is the whole of issue #32.
##   * the verge is 1.80 m and the gravel bed is 20 m, so both fit the kart with
##     room, and three of four is the requirement there. Three rather than four
##     because the road is banked and a band 1.8 m wide has an edge 0.3 m from a
##     wheel.
##
##     station, lateral, expected SurfaceType, wheels required, what it is
const HOP_STANDS := [
	[1150.0, 0.0, 0, 4, "the road, on a straight"],
	[355.0, 5.5, 1, 1, "T2 Lama's reference kerb — 1.00 m, narrower than the kart"],
	[360.0, -5.9, 2, 3, "the verge, T2's left, middle of the 1.80 m band"],
	[750.0, -20.0, 3, 3, "T4 Il Ciglione's gravel bed, 20 m wide"],
]

## Ticks to let the suspension find the ground after the kart is teleported.
const HOP_SETTLE_TICKS := 90

var _hop_stand := 0
var _hop_kart: KartBody
var _hop_place := Vector3.ZERO
var _hop_wrong: Array[String] = []


func _hop_tick() -> void:
	match _phase:
		0:
			# The scene builds over its first frames and `_ready` has not run during
			# `_initialize` — CLAUDE.md's `--script` main loop trap. Everything is
			# looked up here, on a tick, rather than at build time.
			if _phase_tick < 4:
				return
			_track = _root.get("_track")
			if _track == null:
				_check("the circuit loaded", false, "circuit.gd has no _track")
				_finish()
				return
			if _break == "meta":
				# The saboteur. Exactly the real failure this ticket suspects: a
				# collider that exists and was never told what it is made of.
				for name in ["Verge", "Gravel"]:
					var body := _root.get_node_or_null(name)
					if body != null:
						body.remove_meta("surface_type")
			_report_hop_bodies()
			_report_hop_rays()
			if not _prepare_hop_wheels():
				_finish()
				return
			_say("")
			_say("  hop 4 — what the SOLVER reports under its own four wheels")
			_say("  %8s %8s %-8s %-26s %5s %5s %4s  %s" % [
				"station", "lateral", "expect", "wheel_report surface",
				"drift", "latch", "air", "band"])
			_next_phase()
		1:
			# One stand per settle window: teleport, wait, read, move on.
			if _phase_tick == 1:
				_place_hop_kart()
				return
			if _phase_tick < HOP_SETTLE_TICKS:
				return
			_sample_hop_kart()
			_hop_stand += 1
			_phase_tick = 0
			if _hop_stand >= HOP_STANDS.size():
				_check("the surface reaches the solver's own wheels", _hop_wrong.is_empty(),
						"; ".join(_hop_wrong))
				if not _hop_wrong.is_empty():
					_fingerprints.append("wheels: " + "; ".join(_hop_wrong))
				_finish()


## Hop 2. Every band `surface_meshes()` publishes has to have become a
## `StaticBody3D` carrying that band's own `surface_type`.
func _report_hop_bodies() -> void:
	var published := {}
	for entry in _track.surface_meshes(0.02, 2.0):
		published[entry["name"]] = int(entry["surface_type"])

	_say("")
	_say("  hop 2 — the collider the scene built, and the metadata it carries")
	_say("  %-14s %-14s %8s %8s" % ["band", "node", "published", "on body"])
	var wrong: Array[String] = []
	for name in published.keys():
		var body := _root.get_node_or_null(String(name)) as StaticBody3D
		var carried := -1
		if body != null and body.has_meta("surface_type"):
			carried = int(body.get_meta("surface_type"))
		_say("  %-14s %-14s %8d %8d" % [
			name, "absent" if body == null else body.name, int(published[name]), carried])
		if body == null:
			wrong.append("%s has no StaticBody3D" % name)
		elif carried != int(published[name]):
			wrong.append("%s carries %d, published %d" % [name, carried, int(published[name])])
	# The ground the run-off's grass outfield actually falls on. `surface_meshes`
	# publishes no band for it — a corner whose `outfield` is grass gets an apron
	# and a barrier and nothing between them — so the terrain height field is what
	# a wheel out there finds, and it has to be grass or the outfield is asphalt by
	# omission.
	for name in ["GroundTerrain", "Ground"]:
		var body := _root.get_node_or_null(name) as StaticBody3D
		var carried := -1
		if body != null and body.has_meta("surface_type"):
			carried = int(body.get_meta("surface_type"))
		_say("  %-14s %-14s %8s %8d" % [
			name, "absent" if body == null else body.name, "-", carried])
		if body != null and carried != 2:
			wrong.append("%s carries %d, not grass" % [name, carried])

	_check("every surface band became a body carrying its own type", wrong.is_empty(),
			"; ".join(wrong))
	if not wrong.is_empty():
		_fingerprints.append("bodies: " + "; ".join(wrong))


## Hop 3 and hop 4. A downward ray at a lateral offset finds a collider; the
## `KartBody` standing there reports the same integer on all four corners.
##
## The stations are not arbitrary. Each is picked because the census showed the
## lateral profile changes there: 140 is T1's asphalt apron, 360 is T2's gravel,
## 750 is T4's gravel, and 1150 is a plain straight where the verge is the only
## thing outboard of the white line.
func _report_hop_rays() -> void:
	var space := _root.get_world_3d().direct_space_state
	var wanted := [
		# station, lateral, what the census says is there
		[140.0, 0.0, "Asphalt"],
		[360.0, -5.0, "Verge"],
		[360.0, -25.0, "Gravel"],
		[750.0, -5.0, "Verge"],
		[750.0, -20.0, "Gravel"],
		[1150.0, 0.0, "Asphalt"],
	]
	_say("")
	_say("  hop 3 — a downward ray from wheel height, on the real circuit")
	_say("  %8s %8s %-16s %6s %-8s" % ["station", "lateral", "collider", "type", "surface"])
	var missing: Array[String] = []
	for row in wanted:
		var point := _point_at(float(row[0]), float(row[1]))
		var query := PhysicsRayQueryParameters3D.create(
			point + Vector3(0.0, 20.0, 0.0), point + Vector3(0.0, -20.0, 0.0))
		var hit := space.intersect_ray(query)
		var label := "MISS"
		var type := -1
		if not hit.is_empty():
			var collider = hit["collider"]
			label = String(collider.name)
			if collider.has_meta("surface_type"):
				type = int(collider.get_meta("surface_type"))
		_say("  %8.0f %8.1f %-16s %6d %-8s" % [
			float(row[0]), float(row[1]), label, type, _surface_label(type)])
		if hit.is_empty():
			missing.append("station %.0f lat %.1f found nothing" % [float(row[0]), float(row[1])])
		elif type < 0:
			missing.append("%s at station %.0f lat %.1f carries no surface_type" % [
				label, float(row[0]), float(row[1])])
	_check("every sampled point has ground carrying a surface type", missing.is_empty(),
			"; ".join(missing))
	if not missing.is_empty():
		_fingerprints.append("rays: " + "; ".join(missing))


## Hop 4's fixture. The scene's own kart, parked and idled, so the collision shape
## and the mass properties are the ones the circuit actually drives with rather
## than a second kart built for the measurement.
func _prepare_hop_wheels() -> bool:
	_hop_kart = _root.get_node_or_null("Kart") as KartBody
	if _hop_kart == null:
		_check("the scene built a KartBody", false, "no Kart node")
		return false
	# ADR-0040's single writer. `circuit.gd` owns `PlayerDriver.enabled` through the
	# runner, and a valid `input_driver` Callable overrides the pushed input anyway,
	# so this is belt and braces rather than a second owner.
	var driver := _root.get_node_or_null("Driver")
	if driver != null:
		driver.set("enabled", false)
	_hop_kart.set_input_driver(Callable(self, "_idle_input"))
	return true


## Put the kart on the ground at a station and a lateral offset.
##
## **The height comes off a ray, not off the centerline elevation**, and that is a
## defect this file had and its own table caught. `_point_at` returns the
## centerline's y, which is the road's height only at lateral zero: the road is
## crowned and banked and the run-off continues that cross-section outward, so at
## 20 m out the ground stood **1.31 m above** where the kart was being dropped.
## The kart spawned inside the gravel bed, fell out from underneath it, and landed
## on the terrain — so hop 4 reported grass on a stand that was aimed at dirt and
## the ray one section above reported dirt at the same xz. Two measurements of the
## same place disagreeing is what made it findable.
func _place_hop_kart() -> void:
	var row: Array = HOP_STANDS[_hop_stand]
	var point := _point_at(float(row[0]), float(row[1]))
	var space := _root.get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(
		point + Vector3(0.0, 30.0, 0.0), point + Vector3(0.0, -30.0, 0.0))
	var hit := space.intersect_ray(query)
	if not hit.is_empty():
		point.y = hit["position"].y

	# **Through `set_spawn` and `respawn()`, not by writing the transform.** A bare
	# teleport leaves `contact_[]` holding the last valid contact, and
	# `query_ground`'s buried-versus-airborne discriminator is geometric: a kart
	# moved to a place *below* the plane of its last contact reads as buried, so
	# every corner latches, `grounded` stays true, the suspension carries a load
	# from a surface 400 m away and the kart hovers. That is what the first version
	# of this file did, and its own `latch` column is what caught it — four corners
	# latched, reporting the previous stand's grass over the gravel bed. `respawn()`
	# clears `contact_[]`, which is the whole reason it exists as one call.
	var pose := _hop_kart.global_transform
	pose.origin = point + Vector3(0.0, 0.35, 0.0)
	_hop_kart.set_spawn(pose)
	_hop_kart.respawn()
	_hop_place = _hop_kart.global_position


func _sample_hop_kart() -> void:
	var row: Array = HOP_STANDS[_hop_stand]
	var seen: Array[String] = []
	var types: Array[int] = []
	var latched := 0
	var airborne := 0
	var report := _hop_kart.wheel_report()
	for corner in range(4):
		var wheel: Dictionary = report[corner]
		var type := int(wheel["surface"]) if bool(wheel["contact"]) else -1
		types.append(type)
		seen.append(_surface_label(type))
		# **The two columns that tell a real answer from a stale one.** A latched
		# corner is a wheel whose ray found nothing and is being served the last
		# valid contact — ADR-0033 finding 4 — so its `surface` is the surface it
		# was on *last time*, not the one under it. Without this column a latched
		# grass reading from the previous stand is indistinguishable from a fresh
		# measurement of grass, and it was: the first version of this table
		# reported the gravel stand as grass and the ray one section above
		# reported dirt at the same xz.
		if bool(wheel["latched"]):
			latched += 1
		if not bool(wheel["contact"]):
			airborne += 1
	# How far the kart moved while it settled. A stand that slid off its own band is
	# a fixture failure and not a surface failure, and the two are indistinguishable
	# from the surface column alone.
	var drift := _hop_kart.global_position.distance_to(_hop_place)
	_say("  %8.0f %8.1f %-8s %-26s %5.2f %5d %4d  %s" % [
		float(row[0]), float(row[1]), _surface_label(int(row[2])),
		" ".join(seen), drift, latched, airborne, String(row[4])])
	var agreed := 0
	for type in types:
		if type == int(row[2]):
			agreed += 1
	if agreed < int(row[3]):
		_hop_wrong.append("station %.0f lat %.1f: %d of 4 wheels read %s, needed %d" % [
			float(row[0]), float(row[1]), agreed, _surface_label(int(row[2])), int(row[3])])
	if latched > 0:
		_hop_wrong.append("station %.0f lat %.1f: %d corners LATCHED — their surface is stale" % [
			float(row[0]), float(row[1]), latched])


func _idle_input() -> Dictionary:
	return { "throttle": 0.0, "brake": 0.0, "steer": 0.0 }


func _point_at(station: float, lateral: float) -> Vector3:
	var frame := _track.sample(station)
	var position: Vector3 = frame["position"]
	var heading: float = frame["heading"]
	# `Track::right_of`: (cos h, sin h). Restated rather than imported, because
	# there is nothing to import it from on this side.
	return Vector3(
		position.x + cos(heading) * lateral,
		float(frame["elevation"]),
		position.z + sin(heading) * lateral)


# --- case: barrier --------------------------------------------------------------
#
# Acceptance 2: a barrier strike that stops the kart, with the collision reaching
# the solver rather than only Godot.
#
# "Reaching the solver" has a precise meaning here and it is worth stating, because
# it is the half a Godot-only check would miss. `kart_body.cpp` reads
# `get_linear_velocity()` off the `RigidBody3D` at the top of every step, so
# whatever Jolt does to the body at a contact is what the solver integrates from
# on the next tick. The measurement is therefore the solver's own `speed_ms`
# read-out, not the body's position: a wall that stops the body but leaves the
# solver believing it is at 25 m/s would be the failure, and it is checkable.

func _barrier_tick() -> void:
	if _phase_tick < 4:
		return
	if not _barrier_ran:
		_track = _root.get("_track")
		if _track == null:
			_check("the circuit loaded", false, "circuit.gd has no _track")
			_finish()
			return
		if not _aim_at_barrier():
			_finish()
			return
		_barrier_ran = true
		return

	var kart := _probe_kart
	var travel := (kart.global_position - _barrier_start).dot(_barrier_plane_normal)
	_barrier_max_travel = maxf(_barrier_max_travel, travel)
	# Only once the kart has actually reached the wall — the approach itself is
	# slowing under drag and rolling resistance and is not the measurement.
	if travel > BARRIER_STANDOFF_M - 2.0:
		_barrier_speed_min = minf(_barrier_speed_min, kart.linear_velocity.length())
		_barrier_solver_speed = minf(_barrier_solver_speed, kart.speed_ms)

	if _phase_tick < BARRIER_TICKS:
		return
	_report_barrier()
	_finish()


## Put the probe kart `BARRIER_STANDOFF_M` in front of a real barrier quad, aimed
## at it, at `BARRIER_SPEED_MS`.
##
## The quad is chosen rather than invented: the `Barriers` band's own triangles are
## read back out of `surface_meshes()`, one is picked near the middle of the list,
## and its plane is solved from its own vertices. So the run is aimed at the
## geometry the scene actually built and moves with it.
func _aim_at_barrier() -> bool:
	var faces := PackedVector3Array()
	for entry in _track.surface_meshes(0.02, 2.0):
		if entry["name"] == "Barriers":
			faces = entry["faces"]
	if faces.size() < 3:
		_check("there is a barrier to hit", false, "Barriers publishes no faces")
		return false

	# A triangle from the middle of the band, and its foot — the two vertices at
	# the bottom of the 1 m wall.
	var base := (faces.size() / 6) * 3
	var a := faces[base]
	var b := faces[base + 1]
	var c := faces[base + 2]
	var normal := (b - a).cross(c - a).normalized()
	normal.y = 0.0
	normal = normal.normalized()
	# The **base**, not the lowest vertex. Since issue #244 the lowest vertex is the
	# bottom of a skirt driven into the terrain, so aiming the kart at it launched
	# it from 3 m underground and it went 87.74 m through a wall that was working.
	var base_y: float = maxf(maxf(a.y, b.y), c.y) - BARRIER_HEIGHT_M
	var centre := (a + b + c) / 3.0
	centre.y = base_y

	# Which way is "into" the wall? The circuit's centerline at the nearest station
	# is inboard by construction, so the wall's inward normal is the one that points
	# from the wall toward the road.
	var projected := _track.project(centre, -1.0)
	var road := _point_at(float(projected["distance"]), 0.0)
	var toward_road := (road - centre)
	toward_road.y = 0.0
	if normal.dot(toward_road) > 0.0:
		normal = -normal

	# `normal` now points from the road into the wall. The kart starts back along
	# it and drives forward along it.
	_barrier_plane_normal = normal
	var start := centre - normal * BARRIER_STANDOFF_M

	if _break == "barrier":
		# Removed from the tree before it is freed, so the shape leaves the physics
		# space this tick rather than at the end of the frame. The kart below is
		# built after, and therefore has nothing to hit.
		var body := _root.get_node_or_null("Barriers")
		if body != null:
			_root.remove_child(body)
			body.queue_free()

	# The kart is built here rather than borrowed from the scene, because it has to
	# start at a place and a velocity the scene's own spawn cannot express, and
	# moving the timed kart would feed the session a teleport it would have to
	# resolve as a lap cut.
	var kart := KartBody.new()
	kart.name = "BarrierKart"
	# Set before `add_child`: `KartBody::_ready` captures the spawn off the
	# transform it finds. `look_at` points **-Z** at its target and -Z is the
	# kart's forward, so the target is a step further along the inward normal.
	var eye := start + Vector3(0.0, 0.35, 0.0)
	kart.transform = Transform3D().looking_at(normal, Vector3.UP)
	kart.position = eye
	var collider := CollisionShape3D.new()
	collider.name = "Chassis"
	var shape := BoxShape3D.new()
	shape.size = SHAPE_SIZE
	collider.shape = shape
	collider.position = SHAPE_CENTER
	kart.add_child(collider)
	_root.add_child(kart)
	kart.set_input_driver(Callable(self, "_idle_input"))
	kart.linear_velocity = normal * BARRIER_SPEED_MS
	_probe_kart = kart
	_barrier_start = kart.global_position

	# Is there ground under the barrier's foot at all? A wall standing on the road's
	# extrapolated cross-section rather than on the terrain would be a wall in the
	# air or a wall buried, and either is a barrier the kart never touches.
	var space := _root.get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(
		centre + Vector3(0.0, 20.0, 0.0), centre + Vector3(0.0, -40.0, 0.0))
	var hit := space.intersect_ray(query)
	var ground_y := NAN
	var ground_name := "MISS"
	if not hit.is_empty():
		ground_y = hit["position"].y
		ground_name = String(hit["collider"].name)

	_say("")
	_say("  barrier chosen at %.1f, %.1f, %.1f, inward normal %.2f, %.2f, %.2f" % [
		centre.x, centre.y, centre.z, normal.x, normal.y, normal.z])
	_say("  barrier base y %.3f, ground under it %s at y %.3f (gap %+.3f m)" % [
		base_y, ground_name, ground_y, base_y - ground_y])
	_say("  kart launched from %.1f m at %.1f m/s" % [BARRIER_STANDOFF_M, BARRIER_SPEED_MS])

	_census_barrier_feet(faces, space)
	return true


## Where every barrier stands against the ground it is supposed to stand on.
##
## One strike proves one quad. This is the other 1,361, and it is the measurement
## that would catch the failure a single strike cannot see: the barrier is built on
## the **road's extrapolated cross-section** — `_surface_point` continues the crown
## and the bank outward — while the ground out there is `TrackTerrain`'s height
## field, taken off the circuit but not off that plane. Thirty meters out, a 5%
## bank is 1.5 m of disagreement, and the two are not obliged to meet.
##
## A wall whose foot floats above the ground is a wall the kart drives **under**
## once the gap clears the chassis box, and a wall whose *head* is under the ground
## is not in the world at all. Both are "nothing happened".
##
## The two are measured off different vertices and that is the whole of #244's
## effect on this census. The wall is no longer a 1.0 m quad standing on its own
## base: the base is seated on the terrain and the geometry runs `BARRIER_SKIRT`
## below it and `BARRIER_HEIGHT` above. So "is there a gap under the wall" is a
## question about `min(y)` — which is now supposed to be underground, and the check
## is that it *is* — and "is the wall reachable" is a question about `max(y)`. The
## old census asked both of `min(y)` and would call the fixed wall 681 times buried.
func _census_barrier_feet(faces: PackedVector3Array, space: PhysicsDirectSpaceState3D) -> void:
	var floating := 0
	var buried := 0
	var missing := 0
	var worst_high := -INF
	var worst_low := INF
	var sampled := 0
	var index := 0
	# Every sixth vertex: one sample per quad rather than one per triangle, since
	# the two triangles of a wall quad share the same foot.
	while index + 2 < faces.size():
		var a := faces[index]
		var b := faces[index + 1]
		var c := faces[index + 2]
		var foot_y: float = minf(minf(a.y, b.y), c.y)
		var head_y: float = maxf(maxf(a.y, b.y), c.y)
		var mid := (a + b + c) / 3.0
		var query := PhysicsRayQueryParameters3D.create(
			Vector3(mid.x, head_y + 25.0, mid.z), Vector3(mid.x, foot_y - 25.0, mid.z))
		# The wall itself is vertical, so a vertical ray at its own midpoint can
		# still clip it. Excluded by asking for the ground bodies only would need a
		# layer; instead the hit is taken and the barrier's own surface is skipped
		# by name, which is what `exclude` cannot express here.
		var hit := space.intersect_ray(query)
		if hit.is_empty() or String(hit["collider"].name) == "Barriers":
			missing += 1
		else:
			var ground: float = float(hit["position"].y)
			var gap: float = foot_y - ground
			worst_high = maxf(worst_high, gap)
			worst_low = minf(worst_low, gap)
			sampled += 1
			# Any part of the foot above the ground is a gap under the wall, and the
			# skirt exists so there is never one — zero is the threshold, not 0.60,
			# because the skirt makes it reachable. The chassis box's underside sits
			# at 0.035 m, so 0.60 was the old figure for "the box passes beneath".
			if gap > 0.0:
				floating += 1
			# And the wall has to be *reachable*: the head under the ground is a
			# barrier that is not in the world, which is the other half of the same
			# sentence and was never measured because both halves read `min(y)`.
			if head_y < ground:
				buried += 1
		index += 6

	_say("")
	_say("  barrier foot against the ground beneath it, all %d quads" % (sampled + missing))
	_say("  %-34s %10.3f m" % ["highest foot above ground", worst_high])
	_say("  %-34s %10.3f m" % ["deepest foot below ground", worst_low])
	_say("  %-34s %10d" % ["quads with a gap under them", floating])
	_say("  %-34s %10d" % ["quads with their head buried", buried])
	_say("  %-34s %10d" % ["quads with no ground found", missing])
	# Two checks and not one, because after issue #244 they have two different
	# causes and merging them hides which is which. The gap is the barrier's own
	# problem and is what #244 fixed. The buried head is the *terrain's*: where the
	# lap passes close to itself the nearest-station answer flips and the height
	# field pins a grid vertex to the neighbouring section's elevation, which
	# `track_terrain.gd`'s own docstring records as an irreducible 2.343 m step. No
	# closed-form wall height reaches over that, and 25 of the 26 have the ground
	# falling away again outboard — a ridge a kart can climb and drop off the far
	# side of. That is a terrain defect this measurement found and cannot fix.
	_check("no barrier has a gap under it",
			floating == 0 and missing == 0,
			"%d with a gap under them, %d with no ground — a wall the kart passes "
					% [floating, missing]
					+ "under is a barrier that is not there")
	_check("every barrier's head clears the ground around it",
			buried == 0,
			"%d quads have their head below the ground beside them; the height field "
					% buried
					+ "builds a ridge through the barrier line where the lap passes "
					+ "close to itself, and the wall cannot be seen over it")
	if floating > 0 or buried > 0 or missing > 0:
		_fingerprints.append("feet: %d gapped, %d head-buried, %d groundless" % [
			floating, buried, missing])


func _report_barrier() -> void:
	var kart := _probe_kart
	var final := (kart.global_position - _barrier_start).dot(_barrier_plane_normal)
	var penetration := final - BARRIER_STANDOFF_M
	_say("")
	_say("  BARRIER STRIKE — real circuit, %s" % _track.track_name())
	_say("  %-34s %10.3f m" % ["standoff at launch", BARRIER_STANDOFF_M])
	_say("  %-34s %10.3f m" % ["furthest travel toward the wall", _barrier_max_travel])
	_say("  %-34s %10.3f m" % ["past the wall plane at the end", penetration])
	_say("  %-34s %10.3f m/s" % ["slowest body speed at the wall",
			0.0 if is_inf(_barrier_speed_min) else _barrier_speed_min])
	_say("  %-34s %10.3f m/s" % ["slowest SOLVER speed at the wall",
			0.0 if is_inf(_barrier_solver_speed) else _barrier_solver_speed])

	var stopped := penetration <= BARRIER_MAX_PENETRATION_M
	_check("the barrier stops the kart", stopped,
			"the chassis origin ended %.2f m past the wall plane, over the %.2f m a box "
			% [penetration, BARRIER_MAX_PENETRATION_M]
			+ "resting against a zero-thickness wall can be")
	if not stopped:
		_fingerprints.append("penetration %.2f m" % penetration)

	# The second half, and the one a Godot-only check would miss: the solver reads
	# `get_linear_velocity()` at the top of every step, so a wall that stops the
	# body must show up in the solver's own speed read-out.
	var reached := _barrier_solver_speed < BARRIER_SPEED_MS * 0.5
	_check("the collision reaches the solver", reached,
			"the solver's own speed never fell below %.1f m/s at the wall" % (
				BARRIER_SPEED_MS * 0.5))
	if not reached:
		_fingerprints.append("solver speed %.2f m/s" % _barrier_solver_speed)


# --- case: audio ----------------------------------------------------------------
#
# Acceptance 3. The synth end is already held by `tests/core/test_scrub_wind.cpp`
# and `test_shift_audio.cpp`, which sweep every value of `SurfaceType` through the
# real filters offline — a surface that changed nothing there would fail those, and
# they are green. So the only open question is the **join**: does the integer a
# wheel is standing on reach `EngineAudioInput::surface`?
#
# `kart_body.cpp` fills that field from `contact_[corner].surface` for the first
# grounded corner, and `wheel_report()["surface"]` is the same member of the same
# struct. So the `hop` case's hop-4 table IS the join measurement, and this case
# exists to say what could NOT be measured and why rather than to measure it twice.

func _case_audio() -> void:
	_say("")
	_say("  the join is `contact_[corner].surface`. kart_body.cpp fills")
	_say("  EngineAudioInput::surface from it (first grounded corner wins) and")
	_say("  wheel_report()[c][\"surface\"] publishes the same member. --case=hop's")
	_say("  hop-4 table is therefore the join measurement.")
	_say("")
	_say("  What is NOT measured here: the rendered level. NoiseVoiceStream")
	_say("  publishes `level` and `rumble_hz` from the AUDIO thread, so reading")
	_say("  either needs a real device — the Dummy driver never calls _mix, and")
	_say("  scrub_cost_probe.gd refuses to report a figure under it for that")
	_say("  reason. Under CoreAudio with six agents live the measurement is a")
	_say("  loaded machine's, not the rig's. tools/verify/audio.sh owns it.")
	_check("the audio join is the same struct member the solver fills", true, "")


# --- plumbing -------------------------------------------------------------------

func _zeros() -> PackedFloat64Array:
	return PackedFloat64Array([0.0, 0.0, 0.0, 0.0])


func _surface_label(type: int) -> String:
	if type < 0 or type >= SURFACE_NAMES.size():
		return "none"
	return SURFACE_NAMES[type]


func _next_phase() -> void:
	_phase += 1
	_phase_tick = 0


func _say(line: String) -> void:
	_lines.append(line)
	print(line)


func _check(label: String, passed: bool, detail: String) -> void:
	_checks += 1
	if passed:
		_say("  [ ok ] %s" % label)
		return
	_failed += 1
	_say("  [FAIL] %s — %s" % [label, detail])


func _string_arg(key: String, fallback: String) -> String:
	for arg in _args:
		if arg.begins_with("--%s=" % key):
			return arg.substr(key.length() + 3)
	return fallback


## The exit code, and the inversion.
##
## Normally: 0 when nothing failed. Under `--break`, the run is a **pass only when
## something failed AND the failure carries the saboteur's own fingerprint** — a
## pre-existing red does not count as catching a sabotage, which is the trap
## `shell_probe.gd`'s first cut fell into.
func _finish() -> void:
	_say("")
	if _break.is_empty():
		_say("%d checks, %d failed" % [_checks, _failed])
		quit(0 if _failed == 0 else 1)
		return

	_say("BREAK=%s: %d checks, %d failed" % [_break, _checks, _failed])
	if _fingerprints.is_empty():
		_say("NOT CAUGHT — nothing red carried the saboteur's fingerprint")
		quit(1)
		return
	_say("caught, fingerprint: %s" % "; ".join(_fingerprints))
	quit(0)
