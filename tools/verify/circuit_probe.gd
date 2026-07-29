extends SceneTree

## The M5 gate: does `track.json` load, does it measure what it claims, and does
## what you drive on agree with what you see?
##
##     tools/verify/circuit.sh
##     godot --headless --path . --script res://tools/verify/circuit_probe.gd -- \
##         --case=schema
##
##   --case=all         every case below (the default)
##   --case=schema      both files load or refuse as they should, including the
##                      deliberately broken negative control
##   --case=measure     the geometry against `docs/circuits/valdirone_nuova.json`'s
##                      published figures, recomputed rather than restated
##   --case=agree       the C++ collider against `gentrack.py`'s manifest — the
##                      "what you see and what you collide with cannot drift
##                      apart" claim, as a number
##   --case=layouts     the reverse layout: stations, hands, grid, furniture
##   --case=timing      the authored sector marks and checkpoints, through the
##                      timer the session actually runs. Issue #180
##   --case=place       put the kart down at forty stations and read the surface
##                      under all four wheels. Needs the scene and the kart mesh
##   --track=res://...  which circuit
##
## ## Why the negative control is the first case and not the last
##
## `input_push_probe.gd --break` is the pattern: a gate with no negative control is
## a gate nobody has proven can fail. `data/tracks/self_intersecting.track.json` is
## a circuit that closes, turns +360 degrees, is 1,105 m long, has legal camber and
## banking, has its grid on a straight — and crosses itself. If it *loads*, this
## probe exits non-zero, because a validator that accepts it would accept anything.
##
## ## What `--case=agree` actually compares
##
## `ARCHITECTURE.md` §11's claim is that the spline is authored once and read
## twice, so the visual mesh and the collision geometry cannot drift apart. They
## share no code — `src/core/track.h` is C++ inside the engine and
## `tools/blender/tracklib/geometry.py` is Python inside Blender — so the claim is
## only true if something measures it. `gentrack.py` writes the road's two edges
## and its centerline every 25 m into `data/tracks/*.manifest.json`, in Godot's
## frame, straight out of its own interpolation; this case asks `KartTrack` for the
## same points and reports the largest disagreement in millimeters.

const DEFAULT_TRACK := "res://data/tracks/valdirone_nuova.track.json"
const BROKEN_TRACK := "res://data/tracks/self_intersecting.track.json"
const MANIFEST := "res://data/tracks/valdirone_nuova.manifest.json"
const SCENE_PATH := "res://scenes/game/valdirone.tscn"

## How far apart the placement case puts the kart down, and how long it settles.
## 120 ticks is one second at the project's physics rate — long enough for the
## suspension to stop ringing and short enough that forty placements are quick.
const PLACEMENT_COUNT := 40
const SETTLE_TICKS := 120

## The design's own published figures, from `docs/circuits/valdirone_nuova.json`'s
## `measured` block. Restated here **as the thing being checked**, not as the
## answer: every one of them is recomputed from the geometry and the delta is what
## is printed. A gate that printed the design's numbers back would measure nothing.
const DESIGN := {
	"length": 1375.1318,
	"longest_straight": 165.0,
	"start_to_first_corner": 88.0,
	"last_corner_to_start": 77.0,
	"starting_straight": 165.0,
	"elevation_range": 12.5473,
	"elevation_low": -10.835,
	"elevation_high": 1.713,
	"min_width": 9.0,
	"max_width": 14.0,
	"worst_ground_slope_pct": 8.95,
	"corners": 8,
}

## What a delta is allowed to be before it is a failure, per figure.
##
## One millimeter on a length would be wrong to demand: the design published its
## nine straight lengths rounded to two decimals and this project re-solved the
## closure from those rounded values, so the lap comes out 12.4 mm short of the
## design's own unrounded figure. Both are inside the regulation's 1 m accuracy for
## a published circuit length (Part I art 11) and every *corner* is bit-for-bit the
## design's, which is what a lap time is made of.
const TOLERANCE := {
	"length": 0.05,
	"longest_straight": 0.02,
	"start_to_first_corner": 0.01,
	"last_corner_to_start": 0.01,
	"starting_straight": 0.02,
	"elevation_range": 0.01,
	"elevation_low": 0.01,
	"elevation_high": 0.01,
	"min_width": 0.001,
	"max_width": 0.001,
	"worst_ground_slope_pct": 0.10,
	"corners": 0.0,
}

var _case := "all"
var _track_path := DEFAULT_TRACK
var _failures := 0
var _track: KartTrack

var _root: Node3D
var _kart: KartBody
var _placements: Array[Dictionary] = []
var _placement := 0
var _stage_tick := 0
var _wants_placement := false
var _done := false


func _initialize() -> void:
	var args := Cmdline.parse()
	_case = Cmdline.as_string(args, "case", "all")
	_track_path = Cmdline.as_string(args, "track", DEFAULT_TRACK)

	if not ClassDB.class_exists("KartTrack"):
		printerr("KartTrack is not registered — build the extension:")
		printerr("    scons target=editor arch=arm64")
		quit(1)
		return

	print("== circuit probe: case %s ==" % _case)

	if _wants("schema"):
		_case_schema()
	if _wants("measure") or _wants("agree") or _wants("layouts") or _wants("timing"):
		_track = KartTrack.new()
		if _track.load(_track_path) != OK:
			_fail("%s does not load, so nothing after this can be measured" % _track_path)
			for problem in _track.problems():
				printerr("    ! ", problem)
			_finish()
			return
	if _wants("measure"):
		_case_measure()
	if _wants("agree"):
		_case_agree()
	if _wants("layouts"):
		_case_layouts()
	if _wants("timing"):
		_case_timing()

	_wants_placement = _wants("place")
	if not _wants_placement:
		_finish()
		return

	var packed: PackedScene = load(SCENE_PATH)
	if packed == null:
		_fail("could not load " + SCENE_PATH)
		_finish()
		return
	_root = packed.instantiate() as Node3D
	get_root().add_child(_root)


func _physics_process(_delta: float) -> bool:
	# `quit()` asks the loop to stop and does not stop it mid-tick, so this runs
	# once more after a case that finished during `_initialize`. Without this guard
	# the placement case's node lookup runs against a scene that was never
	# instantiated and the gate's last line is a null dereference after it has
	# already printed "pass".
	if _done or _root == null:
		return true
	# The scene is parented but not yet inside the tree during `_initialize`, so its
	# `_ready` has not run and it has no children to find. Everything exists by the
	# first physics tick, which is where the lookup belongs — `shoot.gd`,
	# `drive_probe.gd` and `track_probe.gd` all have this shape for the same reason.
	if _kart == null:
		_kart = _root.find_child("Kart", false, false) as KartBody
		if _kart == null:
			_fail("no Kart in the scene — is assets/generated/kart.glb built?")
			_finish()
			return true
		# Held at zero rather than left to the input map: this case is about where
		# the kart is standing, not about driving, and a scripted input is what makes
		# the same command produce the same numbers. ADR-0040's rule — a valid
		# `input_driver` overrides the pushed input.
		_kart.input_driver = func() -> Dictionary:
			return {"throttle": 0.0, "brake": 0.0, "steer": 0.0}
		_build_placements()
		_place(0)
		return false

	_stage_tick += 1
	if _stage_tick < SETTLE_TICKS:
		return false
	_read_placement()
	_placement += 1
	if _placement >= _placements.size():
		_report_placements()
		_finish()
		return true
	_place(_placement)
	return false


func _wants(name: String) -> bool:
	return _case == "all" or _case == name


func _fail(message: String) -> void:
	printerr("  FAIL: " + message)
	_failures += 1


func _finish() -> void:
	_done = true
	if _failures == 0:
		print("== circuit probe: pass ==")
		quit(0)
	else:
		printerr("== circuit probe: %d failure(s) ==" % _failures)
		quit(1)


# --- schema ----------------------------------------------------------------


func _case_schema() -> void:
	print("-- schema")
	var good := KartTrack.new()
	var error := good.load(_track_path)
	if error != OK:
		_fail("%s refused with %d" % [_track_path, error])
		for problem in good.problems():
			printerr("    ! ", problem)
	else:
		print("  %-46s loads, %d problems, content %s"
				% [_track_path.get_file(), good.problems().size(), good.content_hash()])

	# The negative control. It must NOT load, and it must be refused for the right
	# reason — a validator that rejected it for a typo in a field name would pass
	# this check while proving nothing.
	var broken := KartTrack.new()
	var broken_error := broken.load(BROKEN_TRACK)
	if broken_error == OK:
		_fail("%s LOADED. It crosses itself; the self-intersection gate is not "
				% BROKEN_TRACK.get_file()
				+ "working, and every circuit this project ever validates is unchecked.")
		return
	var named := false
	for problem in broken.problems():
		if problem.contains("clear ground of itself"):
			named = true
	print("  %-46s refused with %d, %d problem(s)"
			% [BROKEN_TRACK.get_file(), broken_error, broken.problems().size()])
	for problem in broken.problems():
		print("      ! " + problem)
	if not named:
		_fail("the negative control was refused, but not for crossing itself. "
				+ "It is meant to break exactly one rule; either it now breaks a "
				+ "different one or the separation check has stopped firing.")


# --- measurements ----------------------------------------------------------


func _case_measure() -> void:
	print("-- measured against docs/circuits/valdirone_nuova.json")
	var measured: Dictionary = _track.measurements()
	print("  %-26s %12s %12s %10s" % ["figure", "design", "built", "delta"])
	for key in DESIGN:
		var design: float = DESIGN[key]
		var built := float(measured.get(key, NAN))
		if is_nan(built):
			_fail("nothing reports %s" % key)
			continue
		var delta := built - design
		var allowed: float = TOLERANCE[key]
		var mark := "" if absf(delta) <= allowed else "   <-- over %.3f" % allowed
		print("  %-26s %12.4f %12.4f %+10.4f%s" % [key, design, built, delta, mark])
		if absf(delta) > allowed:
			_fail("%s is %.4f against the design's %.4f" % [key, built, design])

	# Corner by corner, because the lap length is allowed to move by a centimeter
	# and a corner is not: the radii and the turn angles are the design's exactly,
	# and everything a lap time is made of hangs off them.
	print("  %-18s %6s %8s %9s %9s %9s" % ["corner", "hand", "R (m)", "line (m)", "apex", "lock"])
	for index in _track.corner_count():
		var corner := _track.corner(index)
		print("  %-18s %6s %8.2f %9.2f %9.1f %9.1f" % [
			corner["name"], corner["hand"], corner["min_radius"],
			corner["line_radius"], corner["apex_kmh"], corner["lock_ceiling_kmh"],
		])
		# The rule the project got wrong twice, checked here as well as in the
		# loader: nobody drives the centerline, so the ceiling is against the racing
		# line's radius and the threshold is min(grip, lock) and not grip alone.
		var ceiling: float = minf(corner["grip_ceiling_kmh"], corner["lock_ceiling_kmh"])
		if float(corner["apex_kmh"]) > ceiling + 0.05:
			_fail("%s is taken at %.1f km/h above min(grip, lock) = %.1f"
					% [corner["name"], corner["apex_kmh"], ceiling])
		if not corner["has_runoff"] and absf(float(corner["direction_change_deg"])) > 80.0:
			_fail("%s changes direction by %.0f deg with no run-off"
					% [corner["name"], corner["direction_change_deg"]])


# --- the two consumers agree -----------------------------------------------


func _case_agree() -> void:
	print("-- collider against gentrack.py's mesh")
	if not FileAccess.file_exists(MANIFEST):
		_fail("no %s — run tools/blender/gentrack.sh (or the no-Blender form: "
				% MANIFEST
				+ "python3 tools/blender/gentrack.py --stages=geometry,uv,manifest)")
		return
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(MANIFEST))
	if typeof(parsed) != TYPE_DICTIONARY:
		_fail("%s is not an object" % MANIFEST)
		return
	var manifest: Dictionary = parsed

	if absf(float(manifest.get("length_m", 0.0)) - _track.length()) > 1e-3:
		_fail("the manifest was built from a %.4f m circuit and this one is %.4f m; "
				% [manifest.get("length_m", 0.0), _track.length()]
				+ "re-run gentrack.sh")

	var worst := 0.0
	var worst_where := 0.0
	var worst_edge := ""
	var rows: Array = manifest.get("edges", [])
	for row in rows:
		var station := float(row["distance_m"])
		var frame := _track.sample(station)
		var centre: Vector3 = frame["position"]
		var heading: float = frame["heading"]
		var half := float(frame["width"]) * 0.5
		var right := Vector3(cos(heading), 0.0, sin(heading))
		# The cross-section formula from `docs/TRACK_SCHEMA.md`, applied here so the
		# comparison is against the *schema* rather than against whichever consumer
		# happened to be asked first.
		for edge in [["left", -half], ["centre", 0.0], ["right", half]]:
			var lateral: float = edge[1]
			var y := float(frame["elevation"]) \
					- float(frame["crown_pct"]) * 0.01 * absf(lateral) \
					- float(frame["bank_pct"]) * 0.01 * lateral
			var here := Vector3(centre.x + right.x * lateral, y, centre.z + right.z * lateral)
			var theirs: Array = row[edge[0]]
			var gap := here.distance_to(Vector3(theirs[0], theirs[1], theirs[2]))
			if gap > worst:
				worst = gap
				worst_where = station
				worst_edge = edge[0]
	print("  %d sampled stations, worst disagreement %.4f mm on the %s edge at %.1f m"
			% [rows.size(), worst * 1000.0, worst_edge, worst_where])
	# A millimeter. The manifest rounds its coordinates to six decimals, which is a
	# micron, so anything above that is a real difference in the interpolation and
	# not a rounding artifact — and a real difference means the mesh and the
	# collider are two different roads.
	if worst > 0.001:
		_fail("the mesh and the collider disagree by %.4f mm; §11's 'cannot drift "
				% (worst * 1000.0)
				+ "apart' is not true of this pipeline right now")


# --- layouts ---------------------------------------------------------------


func _case_layouts() -> void:
	print("-- layouts")
	var names := _track.layout_names()
	if names.size() < 2:
		_fail("only %d layout(s); ADR-0046 and GAMEDESIGN §10 want an authored reverse"
				% names.size())
	for name in names:
		_track.select_layout(name)
		print("  %-8s reversed=%s  sectors %s  %d checkpoints  %d grid slots" % [
			name, _track.is_reversed(), _track.sector_marks(),
			_track.checkpoints().size(), _track.grid_count(),
		])
		# Stations round-trip. The conversion is its own inverse, which is what makes
		# a reversal an involution rather than a pair of functions that can disagree —
		# and a sector mark on the wrong side of a corner is exactly what a
		# non-involution looks like from the outside.
		for station in [0.0, 137.5, 524.0, 902.0, 1300.0]:
			var back := _track.to_forward(_track.to_station(station))
			if absf(back - station) > 1e-6:
				_fail("%s: station %.1f does not round-trip (%.6f)" % [name, station, back])
		# Every checkpoint is inside the anti-cut spacing, including the wrap. The
		# wrap is the half that is easy to leave out and is exactly where a kart
		# would rejoin.
		var checkpoints := _track.checkpoints()
		for index in checkpoints.size():
			var here := checkpoints[index]
			var next := checkpoints[(index + 1) % checkpoints.size()]
			var spacing := next - here
			if spacing <= 0.0:
				spacing += _track.length()
			if spacing > 100.0 + 1e-6:
				_fail("%s: checkpoints %d and %d are %.1f m apart"
						% [name, index, (index + 1) % checkpoints.size(), spacing])
		# Every grid slot on the road, on a straight, and the whole grid behind the
		# line rather than straddling it.
		for slot in _track.grid_count():
			var placed := _track.grid_transform(slot, 0.0)
			var found := _track.project(placed.origin, -1.0)
			var frame := _track.sample(found["distance"])
			if absf(float(found["lateral"])) + 0.7 + 0.12 > float(frame["width"]) * 0.5 + 1e-6:
				_fail("%s: grid slot %d is %.2f m off the centerline on a %.2f m road"
						% [name, slot + 1, found["lateral"], frame["width"]])
			if absf(float(frame["curvature"])) > 1e-9:
				_fail("%s: grid slot %d is inside a corner" % [name, slot + 1])

	# Both layouts drive the same asphalt, so a corner that is a left one way must
	# be a right the other. Checked rather than assumed, because "reverse is an
	# authored layout" (ADR-0046) means nothing else about it is guaranteed.
	_track.select_layout("forward")
	var hands := []
	for index in _track.corner_count():
		hands.append(_track.corner(index)["hand"])
	_track.select_layout("reverse")
	for index in _track.corner_count():
		var reversed_hand: String = _track.corner(index)["hand"]
		var forward_hand: String = hands[index]
		if reversed_hand == forward_hand:
			_fail("%s is a %s in both layouts, which is geometrically impossible"
					% [_track.corner(index)["name"], forward_hand])
	_track.select_layout("forward")


# --- timing ----------------------------------------------------------------


## The two authored lists, driven through the timer a session actually uses.
##
## Issue #180. This is deliberately **not** a physics lap: what is being checked is
## the join between `track.json` and `KartLapTimer`, and a lap driven by the solver
## measures the driver too — it would fail for a corner nobody could take and pass
## for a checkpoint that was never crossed, because a valid lap is the same shape
## either way. So the kart is a number walking round the circuit at a constant
## speed, which makes every split predictable in closed form: at constant speed a
## sector time is the sector length over the speed, so the splits are the *authored
## stations* divided by 22 and nothing else.
##
## Four things it asks, and each one is a way the join has been wrong:
##
##   1. The merged marks are the union of the two lists. A merge that dropped a
##      checkpoint is a cut detector with a hole in it and looks identical from
##      outside.
##   2. Three sectors, not sixteen. `sector_count()` conflated with `mark_count()`
##      is what the flag in `lap_timing.h` exists to prevent.
##   3. The splits are the authored proportions to the tick, and they add up to the
##      lap. Thirds would pass a test that only checked they add up.
##   4. Skipping one checkpoint-only mark costs the lap. The negative control, and
##      it has to be a *checkpoint* rather than a sector mark, because a timer that
##      only owed the splits would still catch a missing split.
const TIMING_SPEED_MS := 22.0

## How far either side of a mark the cut control jumps, meters. 40 m in one tick is
## 4,800 m/s, eighty times `LAP_MAX_SPEED_MS`, so it is unambiguously a
## discontinuity — and it is short enough to fit between two checkpoints 98.2 m
## apart without clearing a second one, which would make the control test two
## things at once.
const CUT_JUMP_M := 20.0

func _case_timing() -> void:
	print("-- timing")
	var step := 1.0 / float(Engine.physics_ticks_per_second)
	for name in _track.layout_names():
		_track.select_layout(name)
		var length := _track.length()
		var sectors := _track.sector_marks()
		var checkpoints := _track.checkpoints()

		var timer := KartLapTimer.new()
		if not timer.begin_track(sectors, checkpoints, length, step):
			_fail("%s: the authored marks do not make a timeable lap" % name)
			continue

		# 1. The merged set is the union, in order, with 0.0 present once.
		var expected := PackedFloat64Array([0.0])
		for station in sectors:
			expected.append(station)
		for station in checkpoints:
			expected.append(station)
		var unique := PackedFloat64Array()
		expected.sort()
		for station in expected:
			if unique.is_empty() or station - unique[unique.size() - 1] > 1e-3:
				unique.append(station)
		var marks := timer.marks()
		if marks.size() != unique.size():
			_fail("%s: %d authored stations merged to %d marks, expected %d"
					% [name, sectors.size() + checkpoints.size(), marks.size(), unique.size()])
		else:
			for index in marks.size():
				if absf(marks[index] - unique[index]) > 1e-6:
					_fail("%s: mark %d is at %.6f m, the file says %.6f"
							% [name, index, marks[index], unique[index]])

		# 2. Sectors are the splits plus the line, and nothing else.
		if timer.sector_count() != sectors.size() + 1:
			_fail("%s: %d sectors from %d authored splits — a checkpoint became a sector"
					% [name, timer.sector_count(), sectors.size()])

		# 3. A clean lap at a constant speed, from the line.
		_walk(timer, 0.0, length, length)
		_walk(timer, 0.0, length, length) # the out lap does not count, by design
		var splits := timer.last_sectors()
		if not timer.last_was_valid():
			_fail("%s: a clean lap at a constant speed was struck out as %s"
					% [name, timer.last_reason()])
		elif splits.size() != sectors.size() + 1:
			_fail("%s: the lap reported %d splits for %d sectors"
					% [name, splits.size(), timer.sector_count()])
		else:
			var total := 0.0
			for index in splits.size():
				total += splits[index]
				# The authored station over the speed, which is what a constant speed
				# means. Half a tick of tolerance: the walk lands on the mark within one
				# step and the split is an integer number of ticks either side of it.
				var boundary: float = sectors[index] if index < sectors.size() else length
				var predicted := boundary / TIMING_SPEED_MS
				# One tick, because a mark is consumed on the first tick at or past it
				# and the walk lands wherever 0.183 m per tick puts it. Tighter than that
				# is a test of float rounding; looser and thirds would pass, which is the
				# whole thing being ruled out — 458.4 m of even thirds against an
				# authored 524 m is 3.0 s, or 360 ticks.
				if absf(total - predicted) > step + 1e-9:
					_fail("%s: split %d cumulates to %.4f s, %.1f m at %.1f m/s is %.4f s"
							% [name, index + 1, total, boundary, TIMING_SPEED_MS, predicted])
			if absf(total - timer.last_time()) > 1e-9:
				_fail("%s: the splits add to %.6f s and the lap is %.6f s"
						% [name, total, timer.last_time()])
			print("  %-8s %d marks, %d sectors, lap %.3f s at %.1f m/s, splits %s" % [
				name, timer.mark_count(), timer.sector_count(), timer.last_time(),
				TIMING_SPEED_MS, _split_text(splits),
			])

		# 4. The negative control: skip one checkpoint-only mark and the lap dies.
		#
		# The mark has to be a **checkpoint and not a split**, and the 40 m jump over it
		# has to clear no split either — the checkpoints are every 98.2 m and the splits
		# are at 524 m and 902 m, so there is always one. That is the point of the
		# control: a timer that owed only its sector marks would report this lap clean,
		# because nothing it was watching was missed.
		var boundaries := PackedFloat64Array([0.0])
		for station in sectors:
			boundaries.append(station)
		boundaries.append(length)
		var skipped := -1
		for index in range(1, marks.size()):
			if sectors.has(marks[index]):
				continue
			var clear := true
			for boundary in boundaries:
				if absf(marks[index] - boundary) <= CUT_JUMP_M:
					clear = false
			if clear:
				skipped = index
				break
		if skipped < 0:
			_fail("%s: no checkpoint-only mark clear of a split, so the cut control cannot run"
					% name)
			continue
		var laps_before := timer.laps_completed()
		var before := marks[skipped] - CUT_JUMP_M
		var after := marks[skipped] + CUT_JUMP_M
		_walk(timer, 0.0, before, length)
		# One step over the mark, far larger than a kart can travel in a tick. The marks
		# in between stay owed, which is the only evidence a cut happened.
		timer.advance(after, false)
		_walk(timer, after, length, length)
		# **The lap has to exist and be struck out, not vanish.** `lap_timing.h`: *"a cut
		# has to still complete its lap so that the missed mark can invalidate it —
		# swallowing the lap entirely would leave a driver who cut a corner with no lap
		# on the screen at all and no explanation."* This is where that stopped being
		# true: with fourteen checkpoints the first mark is 98 m past the line, and a cut
		# over it left `next_mark_` at 1 for the whole lap, which the timer read as a kart
		# that had not gone round.
		if timer.laps_completed() != laps_before + 1:
			_fail("%s: a cut over the checkpoint at %.1f m produced no lap at all"
					% [name, marks[skipped]])
		elif timer.last_reason() != "missed_mark":
			_fail("%s: skipping the checkpoint at %.1f m left the lap %s"
					% [name, marks[skipped], timer.last_reason()])
		else:
			print("  %-8s cut over the checkpoint at %.1f m: lap %.3f s, %s" % [
				name, marks[skipped], timer.last_time(), timer.last_reason(),
			])
	_track.select_layout("forward")


## Walk the timer from one arc length to another at `TIMING_SPEED_MS`, one tick at
## a time. Indexed from `from` rather than accumulated: 7,500 additions of 0.183 m
## land a hair short of the length, the final step that crosses the line is never
## fed in, and the lap simply never closes — which reads exactly like a timer bug
## and is a harness bug. `tests/core/test_lap_timing.cpp` carries the same note.
func _walk(timer: KartLapTimer, from: float, to: float, length: float) -> void:
	var step := 1.0 / float(Engine.physics_ticks_per_second)
	var per_tick := TIMING_SPEED_MS * step
	var ticks := int(ceil((to - from) / per_tick))
	for index in range(1, ticks + 1):
		var position := from + per_tick * float(index)
		if position > to or index == ticks:
			position = to
		while position >= length:
			position -= length
		timer.advance(position, false)


func _split_text(splits: PackedFloat64Array) -> String:
	var parts := PackedStringArray()
	for value in splits:
		parts.append("%.3f" % value)
	return " / ".join(parts)


# --- placement -------------------------------------------------------------


## Put the kart down every `length / PLACEMENT_COUNT` meters and read what its four
## wheels are standing on.
##
## This is the "driveable end to end" half of the M5 accept and it is deliberately
## a *placement* sweep rather than a lap: a lap measures the driver as well as the
## road, and what is being asked here is whether the collision geometry exists and
## is where the schema says, everywhere, including the 9 m corner, the 14 m
## hairpin, the 4.60% descent and both ends of every width taper.
func _build_placements() -> void:
	var track := KartTrack.new()
	track.load(_track_path)
	for index in PLACEMENT_COUNT:
		var station := track.length() * index / PLACEMENT_COUNT
		_placements.append({"station": station})


func _place(index: int) -> void:
	var track := KartTrack.new()
	track.load(_track_path)
	var frame := track.sample(_placements[index]["station"])
	var heading: float = frame["heading"]
	var position: Vector3 = frame["position"]
	_kart.set_spawn(Transform3D(Basis(Vector3.UP, -heading),
			position + Vector3(0.0, KartRig.SPAWN_LIFT, 0.0)))
	_kart.respawn()
	_stage_tick = 0


func _read_placement() -> void:
	var placement: Dictionary = _placements[_placement]
	var counts := {}
	for wheel in _kart.wheel_report():
		var type := int(wheel.get("surface_type", 0))
		counts[type] = int(counts.get(type, 0)) + 1
	placement["surfaces"] = counts
	placement["height"] = _kart.global_position.y
	var track := KartTrack.new()
	track.load(_track_path)
	var frame := track.sample(placement["station"])
	placement["road_y"] = float(frame["elevation"])


func _report_placements() -> void:
	print("-- placement: %d stations, kart dropped and settled for %d ticks"
			% [_placements.size(), SETTLE_TICKS])
	var all_asphalt := 0
	var worst_drop := 0.0
	var worst_at := 0.0
	for placement in _placements:
		var counts: Dictionary = placement["surfaces"]
		var asphalt := int(counts.get(0, 0))
		if asphalt == 4:
			all_asphalt += 1
		else:
			var pieces := PackedStringArray()
			for type in counts:
				pieces.append("%d x %d" % [counts[type], type])
			print("    %8.1f m  NOT ALL ASPHALT: %s" % [placement["station"], ", ".join(pieces)])
		# How far the chassis origin ended up below the road's own surface. It should
		# be a ride height above it, not below; anything materially negative is the
		# kart having fallen through a hole in the collider.
		var drop := float(placement["road_y"]) - float(placement["height"])
		if drop > worst_drop:
			worst_drop = drop
			worst_at = placement["station"]
	print("  %d of %d stations report four wheels on asphalt" % [all_asphalt, _placements.size()])
	print("  deepest the chassis sat below the road surface: %.3f m at %.1f m"
			% [worst_drop, worst_at])
	if all_asphalt != _placements.size():
		_fail("%d station(s) do not have four wheels on the road"
				% (_placements.size() - all_asphalt))
	# 0.15 m of slack: the chassis origin is on the ground plane of the body and the
	# suspension compresses under static load, so a small positive number here is
	# the kart sitting down rather than falling through.
	if worst_drop > 0.15:
		_fail("the kart sat %.3f m below the road at %.1f m, which is a hole in the "
				% [worst_drop, worst_at] + "collider rather than a ride height")
