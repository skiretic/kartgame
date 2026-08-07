extends SceneTree

## The M7 racing-line gate: is the line on the road, and does the speed profile
## promise anything the kart cannot do?
##
##     tools/verify/line.sh
##     godot --headless --path . --script res://tools/verify/line_probe.gd -- \
##         --case=valdirone
##
##   --case=all         every case below (the default)
##   --case=model       the ceilings, read out of `tire.h` and `chassis.h` at run
##                      time, printed so a reader can see what everything else was
##                      measured against
##   --case=valdirone   the real circuit, both authored layouts, through `KartTrack`
##   --case=testtrack   the 1,030 m test loop, both directions, through
##                      `TrackLayout` — which has no `sample()`, so it goes through
##                      the station-by-station path instead
##   --case=grip        the same circuit at four tire strengths and three grip
##                      usages: more grip may never be a slower lap
##   --break=<mode>     the negative controls. **Exit code is INVERTED**: each
##                      sabotage must be caught or this fails. Six modes:
##                      corridor, unsolved, nocourse, noline, grip, rollover
##
## Two of the six found real defects the day they were written, which is the
## argument for having them: `--break=noline` caught the centerline objective
## being a *different integral* from the line's, so "the line beats the
## centerline" passed by 0.7% on a line that had not moved; and the first
## `--break=corridor` used `set_edge_margin(-4.0)`, which the setter clamps to
## zero, so it proved nothing while printing a confident number.
##
## ## The one rule this gate is built around
##
## **Nothing here asserts a lap time, a corner speed or a number of g.** Issue
## #137 is open, the tire model is expected to move, and a gate written against
## an absolute figure would go red on the day the kart got better - failing in a
## way nobody could tell apart from a real regression.
##
## What is asserted instead is a set of relations. The profile never demands more
## than the model permits, on either axis. The line never leaves the corridor.
## More grip is never a slower lap. Every one of those survives a tire change and
## every one of them catches a line that is wrong.
##
## Figures *are* printed, in quantity, because "which one bound and by how much"
## is the question the next session will ask. Printed is not gated.
##
## ## Why both a `KartTrack` path and a hand-filled one
##
## `KartTrack.sample()` answers with curvature, width, grade and bank, so
## Valdirone goes through `build_from_course` in two lines. The test track's
## `TrackLayout` publishes `{position, heading, distance}` and nothing else - no
## curvature, no width, and `TrackRibbon.TRACK_WIDTH` is a separate class's
## constant. So the test track is walked here from `TrackLayout`'s own segment
## constants and fed station by station. Both paths are exercised deliberately:
## the second one is what any future course will use, and a binding whose only
## caller is the convenient path is a binding whose other half is untested.

const VALDIRONE := "res://data/tracks/valdirone_nuova.track.json"

## Stations roughly this far apart. 1.5 m puts 1,024 stations on Valdirone and on
## the test track, which is the class's maximum grid and 8 samples across the
## tightest line radius either circuit contains.
const SPACING := 1.5

## How much of the ellipse the profile is allowed to ask for. One, plus a
## tolerance for the fact that a bisection stops at 20 halvings and a Menger
## curvature is measured off chords.
const UTILIZATION_CEILING := 1.0 + 1e-4

var _case := "all"
var _break := ""
var _failures := 0
var _checks := 0


func _initialize() -> void:
	var args := Cmdline.parse()
	_case = Cmdline.as_string(args, "case", "all")
	_break = Cmdline.as_string(args, "break", "")

	if not ClassDB.class_exists("KartRacingLine"):
		printerr("KartRacingLine is not registered - build the extension:")
		printerr("    PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \\")
		printerr("        scons target=editor arch=arm64")
		quit(1)
		return

	if _break != "":
		_run_break()
		return

	print("== line probe: case %s ==" % _case)
	if _wants("model"):
		_case_model()
	if _wants("valdirone"):
		_case_valdirone()
	if _wants("testtrack"):
		_case_testtrack()
	if _wants("grip"):
		_case_grip()
	_finish()


func _wants(name: String) -> bool:
	return _case == "all" or _case == name


func _fail(message: String) -> void:
	printerr("  FAIL: " + message)
	_failures += 1


func _check(condition: bool, message: String) -> void:
	_checks += 1
	if not condition:
		_fail(message)


func _finish() -> void:
	if _failures == 0:
		print("== line probe: pass (%d checks) ==" % _checks)
		quit(0)
	else:
		printerr("== line probe: %d failure(s) of %d checks ==" % [_failures, _checks])
		quit(1)


# --- the model ---------------------------------------------------------------


## What the profile is measured against, printed rather than restated.
##
## Every figure here comes out of `tire.h`, `chassis.h` and `vehicle.h` at run
## time. When issue #137 lands these numbers move and nothing in this file has to
## be edited - which is the whole design of the gate.
func _case_model() -> void:
	print("-- model, read from tire.h / chassis.h / vehicle.h at run time")
	var line := KartRacingLine.new()
	var model: Dictionary = line.model()
	print("   mass %.2f kg   CoM height %.4f m   rolling radius %.4f m"
		% [model["mass"], model["com_height"], model["rolling_radius"]])
	print("   rollover  left %.4f g   right %.4f g   (arms %.4f / %.4f m)"
		% [model["rollover_left_g"], model["rollover_right_g"],
			model["rollover_arm_left"], model["rollover_arm_right"]])
	print("   tire      left %.4f g   right %.4f g   (peak_friction %.3f)"
		% [model["tire_left_g"], model["tire_right_g"], model["peak_friction"]])
	print("   binding   left %.4f g   right %.4f g"
		% [model["lateral_left_g"], model["lateral_right_g"]])
	print("   braking %.4f g   traction %.1f N (%.4f g)   rev-limited top %.1f km/h"
		% [model["brake_g"], model["traction_force"], model["traction_g"],
			model["speed_ceiling_kmh"]])
	print("   sprockets %d/%d   grip usage %.2f"
		% [model["engine_sprocket_teeth"], model["axle_sprocket_teeth"], model["grip_usage"]])

	# The asymmetry is the reason a validation scenario that only ever turns one
	# way measures one of two karts. If it ever vanishes, `chassis.h`'s lump table
	# has lost its offset engine and every rollover figure in the project is wrong.
	_check(model["rollover_right_g"] > model["rollover_left_g"] * 1.05,
		"the two rollover thresholds are the same, so the offset engine is gone from chassis.h")
	_check(model["lateral_left_g"] <= model["rollover_left_g"] + 1e-9,
		"the left-hand lateral ceiling is above the tipping point, which is not a corner")
	_check(model["lateral_right_g"] <= model["rollover_right_g"] + 1e-9,
		"the right-hand lateral ceiling is above the tipping point")
	_check(model["speed_ceiling_kmh"] > 135.0,
		"top gear at the limiter cannot reach the class's own top-speed band")


# --- Valdirone -----------------------------------------------------------------


func _case_valdirone() -> void:
	print("-- Valdirone Nuova, through KartTrack")
	for layout in ["forward", "reverse"]:
		var track := KartTrack.new()
		if track.load(VALDIRONE) != OK:
			_fail("%s does not load" % VALDIRONE)
			for problem in track.problems():
				printerr("    ! ", problem)
			return
		if not track.select_layout(layout):
			_fail("no layout named %s" % layout)
			continue
		var line := KartRacingLine.new()
		if not line.build_from_course(track, SPACING):
			_fail("the line would not build on the %s layout" % layout)
			continue
		_report("valdirone/" + layout, line)
		_assert_sane("valdirone/" + layout, line, track)


# --- the test track ------------------------------------------------------------


## `TrackLayout`'s eight segments as (length, curvature) pairs.
##
## Walked here rather than read off `TrackLayout.samples`, and the reason is the
## same one the circuits section of CLAUDE.md gives about the design centerline
## CSV: a sample list is a *rendering* of the geometry at some spacing, and
## resampling one at a different spacing is a second approximation on top of the
## first. `TrackLayout` publishes the constants and solves the two free straights;
## those are normative and this walks them.
##
## `reversed` negates every curvature and reverses the order, which turns the two
## left-hand corners into right-handers. That is the cheap way to reach the 2.81 g
## side of the rollover threshold, and `TrackLayout`'s own header says so.
func _test_track_segments(reversed: bool) -> Array:
	var layout := TrackLayout.new()
	var segments := [
		[TrackLayout.START_STRAIGHT, 0.0],
		[deg_to_rad(absf(TrackLayout.KINK_ANGLE)) * TrackLayout.KINK_RADIUS,
			signf(TrackLayout.KINK_ANGLE) / TrackLayout.KINK_RADIUS],
		[layout.braking_straight, 0.0],
		[deg_to_rad(absf(TrackLayout.HAIRPIN_ANGLE)) * TrackLayout.HAIRPIN_RADIUS,
			signf(TrackLayout.HAIRPIN_ANGLE) / TrackLayout.HAIRPIN_RADIUS],
		[TrackLayout.POWER_STRAIGHT, 0.0],
		[deg_to_rad(absf(TrackLayout.SWEEPER_ANGLE)) * TrackLayout.SWEEPER_RADIUS,
			signf(TrackLayout.SWEEPER_ANGLE) / TrackLayout.SWEEPER_RADIUS],
		[layout.approach_straight, 0.0],
		[deg_to_rad(absf(TrackLayout.LONG_ANGLE)) * TrackLayout.LONG_RADIUS,
			signf(TrackLayout.LONG_ANGLE) / TrackLayout.LONG_RADIUS],
	]
	if not reversed:
		return segments
	var flipped := []
	for index in range(segments.size() - 1, -1, -1):
		flipped.append([segments[index][0], -segments[index][1]])
	return flipped


func _case_testtrack() -> void:
	print("-- the test track, through TrackLayout's own segment constants")
	for reversed in [false, true]:
		var segments := _test_track_segments(reversed)
		var total := 0.0
		for segment in segments:
			total += float(segment[0])

		var line := KartRacingLine.new()
		var count: int = line.begin(total, SPACING)
		if count <= 0:
			_fail("the test track reports no length")
			return

		# One exact walk of the segment list, evaluated at each station. Straights
		# advance along the heading, arcs turn about their own center - the same two
		# expressions `track.h::sample` and `track_layout.gd::_arc` both use, so a
		# sign error here would disagree with both.
		var x := 0.0
		var z := 0.0
		var heading := 0.0
		var walked := 0.0
		var segment_index := 0
		var into := 0.0
		for index in range(count):
			var want: float = line.spacing() * float(index)
			while want > walked + float(segments[segment_index][0]) - into + 1e-9 \
					and segment_index < segments.size() - 1:
				var remaining: float = float(segments[segment_index][0]) - into
				var pair := _advance(x, z, heading, remaining, float(segments[segment_index][1]))
				x = pair[0]
				z = pair[1]
				heading = pair[2]
				walked += remaining
				into = 0.0
				segment_index += 1
			var step := want - walked
			var moved := _advance(x, z, heading, step, float(segments[segment_index][1]))
			line.set_station(index, Vector3(moved[0], 0.0, moved[1]), moved[2],
				float(segments[segment_index][1]), 0.5 * TrackRibbon.TRACK_WIDTH, 0.0, 0.0, 1.0)
			x = moved[0]
			z = moved[1]
			heading = moved[2]
			walked = want
			into += step

		if not line.solve():
			_fail("the test track line would not solve")
			return
		var name := "testtrack/" + ("reverse" if reversed else "forward")
		_report(name, line)
		_assert_sane(name, line, null)

		# The reversed lap turns the same corners the other way, so its tightest
		# line radius must be the same to within the discretization. If it is not,
		# the curvature sign flipped something it should not have.
		var summary: Dictionary = line.summary()
		var expected := TrackLayout.HAIRPIN_RADIUS
		_check(summary["centerline_min_radius"] > expected * 0.98
				and summary["centerline_min_radius"] < expected * 1.02,
			"%s: the centerline's tightest radius reads %.2f m against the hairpin's %.2f m"
				% [name, summary["centerline_min_radius"], expected])


## One step along a straight or around an arc. Returns `[x, z, heading]`.
func _advance(x: float, z: float, heading: float, step: float, curvature: float) -> Array:
	if step <= 0.0:
		return [x, z, heading]
	if is_zero_approx(curvature):
		return [x + sin(heading) * step, z - cos(heading) * step, heading]
	var radius := 1.0 / curvature
	var center_x := x + cos(heading) * radius
	var center_z := z + sin(heading) * radius
	var turned := heading + curvature * step
	return [center_x - cos(turned) * radius, center_z - sin(turned) * radius, turned]


# --- grip ----------------------------------------------------------------------


## The property that makes this gate survive issue #137, measured on the real
## circuit rather than argued for in a comment.
func _case_grip() -> void:
	print("-- grip: more of it is never a slower lap")
	var track := KartTrack.new()
	if track.load(VALDIRONE) != OK:
		_fail("%s does not load" % VALDIRONE)
		return

	print("     usage   lap s   apex km/h   top km/h   worst lat   worst ellipse")
	var previous_lap := 0.0
	for usage in [1.0, 0.9, 0.8, 0.6]:
		var line := KartRacingLine.new()
		line.set_grip_usage(usage)
		if not line.build_from_course(track, SPACING):
			_fail("the line would not build at grip usage %.2f" % usage)
			return
		var summary: Dictionary = line.summary()
		print("     %5.2f  %6.3f     %7.2f    %7.2f      %.6f        %.6f"
			% [usage, summary["lap_time"], summary["min_speed_kmh"], summary["max_speed_kmh"],
				line.worst_lateral_utilization(), line.worst_combined_utilization()])
		_check(summary["lap_time"] >= previous_lap - 1e-6,
			"grip usage %.2f is faster than %.2f, which is backwards" % [usage, previous_lap])
		_check(line.worst_combined_utilization() <= UTILIZATION_CEILING,
			"grip usage %.2f asks for %.6f of the ellipse"
				% [usage, line.worst_combined_utilization()])
		previous_lap = summary["lap_time"]

	# And the sprockets, because `gearbox.h` calls them the one thing a track
	# author is expected to change. A taller final drive must not make the lap
	# faster *and* slower at once - it must move the gear histogram.
	var tall := KartRacingLine.new()
	tall.set_sprockets(18, 22)
	if tall.build_from_course(track, SPACING):
		var standard := KartRacingLine.new()
		standard.build_from_course(track, SPACING)
		var a: Array = standard.summary()["gear_stations"]
		var b: Array = tall.summary()["gear_stations"]
		print("     gear stations 18/25 %s -> 18/22 %s" % [a, b])
		_check(a != b, "a taller final drive changed nothing, so the gearing is not being read")


# --- reporting and the assertions ----------------------------------------------


func _report(name: String, line: KartRacingLine) -> void:
	var summary: Dictionary = line.summary()
	print("   %s" % name)
	print("     %d stations at %.3f m   centerline %.1f m -> line %.1f m"
		% [summary["stations"], line.spacing(), summary["centerline_length"],
			summary["line_length"]])
	print("     curvature integral  %.5f -> %.5f  (%+.1f%%)   ladder %s"
		% [summary["centerline_objective"], summary["line_objective"],
			100.0 * (summary["line_objective"] / maxf(summary["centerline_objective"], 1e-12) - 1.0),
			_ladder(summary["level_objective"])])
	print("     tightest radius     %.2f m -> %.2f m   max offset %.3f m   corridor slack %.4f m"
		% [summary["centerline_min_radius"], summary["min_radius"], summary["max_offset"],
			summary["corridor_slack"]])
	print("     lateral   max %.3f g at %.1f m   ceilings: tire floor %.3f g  rollover floor %.3f g"
		% [summary["max_lateral_g"], summary["max_lateral_g_at"],
			summary["min_tire_ceiling_g"], summary["min_rollover_ceiling_g"]])
	print("     rollover-bound stations %d of %d"
		% [summary["rollover_bound_stations"], summary["stations"]])
	# The parentheses are load bearing. GDScript binds `%` tighter than `+`, so a
	# format string split across two literals applies the arguments to the second
	# half only and prints the first half's placeholders verbatim - which is what
	# this line did, silently, while every number in it was correct.
	print(("     braking   max %.3f g at %.1f m (limit %.3f g + air)   traction max %.3f g "
			+ "(limit %.3f g)")
		% [summary["max_braking_g"], summary["max_braking_g_at"], summary["brake_limit_g"],
			summary["max_traction_g"], summary["traction_limit_g"]])
	print("     speed     %.1f - %.1f km/h   lap %.3f s (reported, never gated)"
		% [summary["min_speed_kmh"], summary["max_speed_kmh"], summary["lap_time"]])
	print("     zones %d   shifts %d   gears %s"
		% [summary["braking_zones"], summary["shifts"], summary["gear_stations"]])
	print("     utilization  lateral %.6f   ellipse %.6f"
		% [line.worst_lateral_utilization(), line.worst_combined_utilization()])
	var points: Array = line.braking_points()
	var text := PackedStringArray()
	for entry in points:
		text.append("%.0f m @ %.0f km/h g%d"
			% [entry["station"], entry["speed_kmh"], entry["gear"]])
	print("     braking points: %s" % ", ".join(text))


func _ladder(levels: Array) -> String:
	var text := PackedStringArray()
	for value in levels:
		text.append("%.4f" % value)
	return "[" + " ".join(text) + "]"


## Everything that must be true of any line on any circuit.
##
## `course` may be null: it is only used for the corridor cross-check, which
## needs the road's own width at each station and only `KartTrack` has one.
func _assert_sane(name: String, line: KartRacingLine, course: Object) -> void:
	var summary: Dictionary = line.summary()

	# 1. The gates. Neither names a number of g, and that is the point.
	_check(line.worst_lateral_utilization() <= UTILIZATION_CEILING,
		"%s: the profile asks for %.6f of the lateral ceiling"
			% [name, line.worst_lateral_utilization()])
	_check(line.worst_combined_utilization() <= UTILIZATION_CEILING,
		"%s: the profile asks for %.6f of the friction ellipse"
			% [name, line.worst_combined_utilization()])

	# 2. The line is on the road, checked against the course's own width rather
	#    than against the corridor the class computed for itself.
	_check(summary["corridor_slack"] >= -1e-6,
		"%s: the line leaves its corridor by %.4f m" % [name, -summary["corridor_slack"]])
	if course != null and course.has_method("sample"):
		var worst := 0.0
		var worst_at := 0.0
		for index in range(0, line.station_count(), 7):
			var station: Dictionary = line.station(index)
			var sample: Dictionary = course.sample(station["station"])
			var room: float = 0.5 * float(sample["width"]) - line.get_edge_margin()
			var over: float = absf(station["offset"]) - room
			if over > worst:
				worst = over
				worst_at = station["station"]
		_check(worst <= 1e-6,
			"%s: the line is %.4f m outside the road at %.1f m" % [name, worst, worst_at])

	# 3. The line is a line: it opened the corners out rather than tightening them,
	#    and its curvature integral beats the centerline's.
	_check(summary["line_objective"] < summary["centerline_objective"],
		"%s: the optimized line's curvature integral %.5f is no better than the centerline's %.5f"
			% [name, summary["line_objective"], summary["centerline_objective"]])
	_check(summary["min_radius"] > summary["centerline_min_radius"],
		"%s: the line's tightest radius %.2f m is tighter than the centerline's %.2f m"
			% [name, summary["min_radius"], summary["centerline_min_radius"]])
	_check(summary["line_length"] > summary["centerline_length"] * 0.9,
		"%s: the line is %.1f m against a centerline of %.1f m, which is not a lap"
			% [name, summary["line_length"], summary["centerline_length"]])

	# 4. It is a lap that could be driven: it moves, it uses the gearbox, and it
	#    never over-revs the engine.
	_check(summary["min_speed_kmh"] > 1.0,
		"%s: the profile stops the kart at %.2f km/h" % [name, summary["min_speed_kmh"]])
	_check(summary["braking_zones"] >= 1,
		"%s: a lap with corners and no braking zone is a profile that did not run" % name)
	var gears: Array = summary["gear_stations"]
	_check(int(gears[0]) == 0, "%s: the profile selects neutral while moving" % name)
	var used := 0
	for gear in range(1, gears.size()):
		if int(gears[gear]) > 0:
			used += 1
	_check(used >= 3,
		"%s: only %d gears used on a six-speed, so the profile is not gear aware" % [name, used])

	# 5. Every station individually, which is where a single bad segment hides.
	var model: Dictionary = line.model()
	var over_rev := 0
	var over_ellipse := 0
	for index in range(line.station_count()):
		var station: Dictionary = line.station(index)
		if float(station["rpm"]) > 14800.0 + 1e-6:
			over_rev += 1
		if float(station["combined_utilization"]) > UTILIZATION_CEILING:
			over_ellipse += 1
	_check(over_rev == 0, "%s: %d stations over the hard cut" % [name, over_rev])
	_check(over_ellipse == 0, "%s: %d stations over the ellipse" % [name, over_ellipse])
	_check(model["grip_usage"] == 1.0,
		"%s: the reference line was measured at grip usage %.2f" % [name, model["grip_usage"]])

	# 6. `at_station` is the AI's own reader and it must agree with `station`.
	#    A binding that interpolated into a different array would be invisible to
	#    every check above, because nothing else calls it.
	var probe: int = int(line.station_count() / 3)
	var direct: Dictionary = line.station(probe)
	var interpolated: Dictionary = line.at_station(float(direct["station"]))
	var gap: float = (Vector3(direct["position"]) - Vector3(interpolated["position"])).length()
	_check(gap < 1e-6,
		"%s: at_station() is %.6f m from station(): the two readers disagree" % [name, gap])
	_check(absf(float(direct["speed"]) - float(interpolated["speed"])) < 1e-6,
		"%s: at_station() and station() report different speeds" % name)
	# And it must wrap, because a lap does.
	var wrapped: Dictionary = line.at_station(
		float(direct["station"]) + summary["centerline_length"])
	_check((Vector3(direct["position"]) - Vector3(wrapped["position"])).length() < 1e-6,
		"%s: at_station() does not wrap over the start line" % name)


# --- the negative controls -------------------------------------------------------


## Six sabotages, each of which some check above must catch. **The exit code is
## inverted**: this run fails if the sabotage goes unnoticed.
##
## The house rule the hard way: `shell_probe.gd` shipped six checks that could not
## fail, and the first cut of its `--break` reported "caught" off a pre-existing
## red it had not caused. So each mode below names the *symptom* it expects and
## the run is only a pass if that symptom is what shows up.
func _run_break() -> void:
	print("== line probe --break=%s: the sabotage must be caught ==" % _break)
	var caught := false
	var track := KartTrack.new()
	if track.load(VALDIRONE) != OK:
		printerr("  the circuit does not load, so nothing can be sabotaged")
		quit(1)
		return

	match _break:
		"corridor":
			# Tell the optimizer the road is 6 m wider than it is, then measure
			# against the road's real width. This is the sabotage the corridor
			# cross-check exists for and it is the one that could really happen:
			# a course whose `width` means something other than the drivable road.
			#
			# Note what it is *not*: `set_edge_margin(-4.0)` clamps to zero and
			# would have proved nothing while printing a confident "caught".
			var line := KartRacingLine.new()
			var count: int = line.begin(track.length(), SPACING)
			for index in range(count):
				var station := line.spacing() * float(index)
				var sample: Dictionary = track.sample(station)
				line.set_station(index, sample["position"], sample["heading"],
					sample["curvature"], 0.5 * float(sample["width"]) + 3.0,
					sample["grade"], float(sample["bank_pct"]) / 100.0, 1.0)
			line.solve()
			var worst := 0.0
			var worst_at := 0.0
			for index in range(0, line.station_count(), 7):
				var station: Dictionary = line.station(index)
				var sample: Dictionary = track.sample(station["station"])
				var room: float = 0.5 * float(sample["width"]) - line.get_edge_margin()
				if absf(station["offset"]) - room > worst:
					worst = absf(station["offset"]) - room
					worst_at = station["station"]
			print("     widest excursion outside the road: %+.4f m at %.1f m" % [worst, worst_at])
			caught = worst > 0.5
		"unsolved":
			# Read a line that was never solved. Everything must be zero rather
			# than plausible.
			var line := KartRacingLine.new()
			var summary: Dictionary = line.summary()
			print("     unsolved: stations %d, lap %.3f s" % [summary["stations"],
				summary["lap_time"]])
			caught = int(summary["stations"]) == 0 and not line.is_solved()
		"nocourse":
			# A course with no `sample()`. `build_from_course` must refuse rather
			# than silently produce a line on a circuit it never read.
			var line := KartRacingLine.new()
			var built: bool = line.build_from_course(RefCounted.new(), SPACING)
			print("     build_from_course on an empty object returned %s" % built)
			caught = not built
		"noline":
			# Turn the optimizer off by pinning the corridor shut. The line must
			# then be the centerline, and the "beats the centerline" check must
			# notice that it does not.
			var line := KartRacingLine.new()
			line.set_edge_margin(20.0)
			line.build_from_course(track, SPACING)
			var summary: Dictionary = line.summary()
			print("     corridor shut: max offset %.4f m, objective %.5f against %.5f"
				% [summary["max_offset"], summary["line_objective"],
					summary["centerline_objective"]])
			caught = summary["max_offset"] < 1e-9 \
				and summary["line_objective"] >= summary["centerline_objective"] - 1e-9
		"grip":
			# Halve the grip. Every corner speed must fall and the lap must get
			# slower; a profile that did not read the tire would not move.
			var full := KartRacingLine.new()
			full.build_from_course(track, SPACING)
			var half := KartRacingLine.new()
			half.set_grip_usage(0.5)
			half.build_from_course(track, SPACING)
			var a: float = full.summary()["lap_time"]
			var b: float = half.summary()["lap_time"]
			print("     lap at full grip %.3f s, at half %.3f s" % [a, b])
			caught = b > a * 1.05
		"rollover":
			# Hand the profile a tire no tire could be - peak friction 8, four
			# times the real one - and require that the kart still cannot corner
			# harder than it tips at.
			#
			# The sabotage is the tire, and what must be caught is the profile
			# believing it. With the rollover clamp in place the ceiling stops at
			# `chassis.h`'s threshold and a large number of stations become
			# rollover-bound; take the clamp out and the corner speeds keep
			# climbing to about 8 g, so the `over` count below would be most of
			# the lap. Both fingerprints are demanded, because "no station is over
			# the tipping point" is also true of a profile that never got near it.
			var line := KartRacingLine.new()
			line.set_peak_friction(8.0)
			line.build_from_course(track, SPACING)
			var model: Dictionary = line.model()
			var summary: Dictionary = line.summary()
			var over := 0
			var worst := 0.0
			for index in range(line.station_count()):
				var station: Dictionary = line.station(index)
				var hand: String = "left" if float(station["curvature"]) < 0.0 else "right"
				var ceiling: float = model["rollover_%s_g" % hand]
				# Banking raises the tipping point too, so the flat threshold is
				# only an upper bound where the road is flat. Compare against what
				# the model published for this station instead.
				ceiling = maxf(ceiling, float(station["lateral_ceiling_g"]))
				worst = maxf(worst, float(station["lateral_g"]) - ceiling)
				if float(station["lateral_g"]) > ceiling + 1e-9:
					over += 1
			print("     peak_friction %.1f: %d of %d stations rollover-bound, %d over their own "
				% [line.get_peak_friction(), summary["rollover_bound_stations"],
					summary["stations"], over]
				+ "tipping point, worst excess %+.4f g" % worst)
			caught = over == 0 and int(summary["rollover_bound_stations"]) > 100
		_:
			printerr("  unknown --break mode '%s'" % _break)
			printerr("  modes: corridor unsolved nocourse noline grip rollover")
			quit(1)
			return

	if caught:
		print("== line probe --break=%s: caught, as required ==" % _break)
		quit(0)
	else:
		printerr("== line probe --break=%s: NOT caught ==" % _break)
		quit(1)
