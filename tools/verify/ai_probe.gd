extends SceneTree

## The M7 AI gate: can it drive Valdirone, and does it drive it through the
## player's own input path?
##
##     tools/verify/ai.sh
##     godot --headless --path . --script res://tools/verify/ai_probe.gd -- \
##         --case=lap
##
##   --case=all         every case below (the default)
##   --case=wiring      the node is what ADR-0040 says a producer is: it derives
##                      from `PlayerDriver`, it runs at `PHYSICS_PRIORITY`, the
##                      body consumes what it pushed rather than falling stale,
##                      and every value it pushes is on the replay grid
##   --case=limits      what the controller was told, read out of
##                      `KartRacingLine.model()` at run time and printed so a
##                      reader can see what everything else is measured against
##   --case=hint        the carried station hint agrees with an unhinted walk.
##                      This is `Track::project`'s 687.5 m defect, re-asked from
##                      the AI's side — it hints every tick, so it is on the
##                      exact path that was broken until `b783c61`
##   --case=lap         three timed laps of Valdirone through the real
##                      `SessionRunner`, forward: lap time, track-limits strikes,
##                      ellipse utilization against what the tire permits *now*,
##                      max body slip, max cross-track error
##   --case=reverse     one lap of the authored reverse layout, because a line
##                      solved in the layout's own frame is the half of this that
##                      could silently be solved in the other one
##   --case=tiers       three difficulty tiers by `set_grip_usage`: a lower tier
##                      is never faster
##   --break=<mode>     the negative controls. **Exit code is INVERTED**: each
##                      sabotage must be caught or this fails
##
## ## The one rule this gate is built around
##
## **Nothing here asserts a lap time, a corner speed or a number of g.** Issue
## #137 is open, agent P2 is changing the tire model, and a gate written against
## an absolute would go red on the day the kart got better — failing in a way
## nobody could tell apart from a regression. `line_probe.gd` says the same thing
## in the same words and it is not boilerplate: it is why this file reads its
## ceilings out of `KartRacingLine.model()` on every run and compares against
## *those*.
##
## What is asserted is relations. The kart never demands more of the ellipse than
## the model permits. Laps complete and are valid. A lower tier is never faster.
## Body slip stays out of the departure band. The AI's own hinted walk agrees
## with an unhinted one. Every one of those survives a tire change.
##
## Figures **are** printed, in quantity, because "how fast and by how much" is
## the question tomorrow morning asks. Printed is not gated.
##
## ## Why it drives the real scene
##
## `scenes/game/valdirone.tscn` with `--ai-solo=true`, so the AI is the kart a
## real `SessionRunner` is timing, the real `LapLedger` is filing, and the real
## `KartLapTimer` is striking for track limits. A synthetic harness would have
## measured a controller against a road nobody races on; every defect this
## project has found in its session layer was found at the join and not inside a
## part.
##
## ## `user://`, and why the slug is ugly
##
## Every worktree shares one `user://`. `circuit.gd` builds a `KartProfile` and a
## `KartGhost`, and `KartGhost` has **no** `set_base_dir()` at all — its
## `ghost_directory()` is a hardcoded `user://ghosts` with the id derived from
## track + layout + class. So a probe that drove the real circuit's slug would
## overwrite the real ghost. This one drives a **copy of the track file** under
## `aiprobe_circuit`, which nothing else can collide with, and `_scrub()` deletes
## what it wrote. `walk_probe.gd` learned this the same way and its header says
## so.

const TRACK_SOURCE := "res://data/tracks/valdirone_nuova.track.json"
const CIRCUIT_SCENE := "res://scenes/game/valdirone.tscn"

## The slug the copy is driven under. Nothing else in the project writes it, so a
## ghost or a best lap filed here cannot land on a real one.
const TRACK_SLUG := "aiprobe_circuit"
const PROBE_DIR := "user://ai_probe"
const TRACK_COPY := PROBE_DIR + "/" + TRACK_SLUG + ".track.json"

## How much of the ellipse the kart is allowed to ask for, measured tick by tick
## against ceilings read at run time.
##
## Over one, and by a lot, and the number is honest about what it is. The line's
## own profile is gated at 1.0 by `line_probe.gd` because it is a quasi-static
## optimum evaluated on its own geometry. A **driven** kart is not that: it has a
## transient every time it turns in, the solver's lateral figure is the mean over
## a tick's substeps of a real load transfer, and the ceiling here is a
## point-mass fixed point that knows nothing about either. 1.35 is the band that
## separates "driving hard" from "sliding", and what makes it a check rather than
## a shrug is that it is measured against a ceiling that moves with the tire.
const UTILIZATION_CEILING := 1.35

## Body slip past which the kart has departed rather than cornered, radians.
## `drive_probe.gd`'s number and its reason, unchanged — 35 degrees is where the
## front tires are past their peak slip angle and the kart is a passenger.
const DEPARTURE_RAD := 0.6109

## The AI must stay within this of the line it is following, meters.
##
## **estimated, and it is a corridor rather than a tolerance.** Valdirone runs
## 9.0 to 14.0 m wide and `racing_line.h`'s corridor already insets half a kart
## plus the edge line from each side, so the line has 3.5 m of room on the
## narrowest part of the circuit. A controller that stayed inside 1.5 m of its
## own line never puts a wheel outside the corridor the line was solved in, which
## is the thing that actually matters and is separately gated by the track-limits
## strike count.
const CROSS_TRACK_CEILING := 1.5

## Laps to drive in the `lap` case. Three, because the first is a standing start
## out of the pits and the interesting number is what the AI does when it is
## already at speed.
const LAPS := 3

## Physics ticks before a run is declared hung.
##
## Three laps of Valdirone at the line's own 42 s is about 15,000 ticks, so this
## is a factor of two of room. **It is a ceiling and not a timeout**: a run that
## reaches it has already failed, and the reason it is not larger is that a
## controller which has parked itself must not take ten minutes to say so. The
## stall detector below is what usually ends a broken run.
const TICK_BUDGET := 32000

## Sim seconds of no forward progress before a run is abandoned.
##
## A kart that has spun, stalled or driven into a barrier stops advancing its
## odometer, and every tick after that is measuring nothing. Ending the run here
## rather than at `TICK_BUDGET` is the difference between a `--break=steer`
## control that answers in twenty seconds and one that answers in four minutes;
## with seven controls that is the difference between a gate somebody runs and a
## gate somebody skips.
const STALL_TICKS := 1200

## Print a line of telemetry every this many ticks. Off unless `--trace=true`,
## because a passing run is a table and not a log.
const TRACE_EVERY := 600

var _case := "all"
var _break := ""
var _failures := 0
var _checks := 0
var _notes := PackedStringArray()

var _track: KartTrack
var _circuit: Node
var _runner: SessionRunner
var _kart: KartBody
var _ai: AiDriver

var _phase := 0
var _phase_frame := 0
var _ticks := 0

var _laps: Array = []
var _worst_lateral := 0.0
var _worst_combined := 0.0
var _worst_lateral_at := 0.0
var _max_slip := 0.0
var _max_cross := 0.0
var _min_speed := 1e30
var _max_speed := 0.0
var _stale_at_start := 0
var _shifts := 0
var _last_gear := 0
var _off_grid := 0
var _ceiling_lateral_g := 0.0
var _ceiling_combined_g := 0.0

var _pending: Array = []
var _tier_results: Array = []
var _run_label := ""
var _run_layout := "forward"
var _run_grip := 1.0
var _run_laps := LAPS
var _trace := false
var _stall_ticks := 0
var _last_odometer := 0.0
var _stalled := false


func _initialize() -> void:
	var args := Cmdline.parse()
	_case = Cmdline.as_string(args, "case", "all")
	_break = Cmdline.as_string(args, "break", "")
	_trace = Cmdline.as_bool(args, "trace", false)

	# `ClassDB.class_exists` is honest here and only here: `AiDriver` and
	# `KartRacingLine` are GDExtension classes, so they really are in `ClassDB`.
	# It is **always false for a GDScript `class_name`** — `SessionRunner`,
	# `KartRig` — and a gate that guarded a search behind it would report PASS
	# having looked at nothing. That shape shipped in `shell_probe.gd`.
	for required in ["AiDriver", "KartRacingLine", "KartBody", "PlayerDriver"]:
		if not ClassDB.class_exists(required):
			printerr("%s is not registered - build the extension:" % required)
			printerr("    PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \\")
			printerr("        scons target=editor arch=arm64")
			quit(1)
			return

	if not _copy_track():
		_finish()
		return

	print("== ai probe: case %s%s ==" % [
		_case, "" if _break == "" else "  break=" + _break,
	])

	# **Always, whatever the case.** `_ceiling_lateral_g` is what every driven
	# run's ellipse check divides by, and a subset run that skipped this left it
	# at zero — so that check passed on every path including a kart sliding on
	# its roof, and printed "worst lateral 0.0000" while doing it. That is the
	# "check that cannot fail" shape `shell_probe.gd` shipped six of, and
	# `--case=` not gating a walk is the other half of the same entry. It costs
	# one 38 ms solve; the *printing* is what `--case=limits` gates.
	_case_limits(_wants("limits") or _break == "ceiling")
	if _wants("hint") or _break == "hint":
		_case_hint()

	# The driven cases are queued rather than run here: each one needs the scene
	# tree to turn, which `_initialize` cannot make happen. `_process` walks the
	# queue.
	_queue_runs()
	if _pending.is_empty():
		_finish()


## The driven runs this invocation owes, in order.
##
## **Every case gates its own walk.** `shell_probe.gd` shipped a `--case=` that
## did not gate two of its walks, so a subset run printed a confident number that
## included reds it had never been asked about. Here a case that is not wanted
## contributes no run and therefore no check.
func _queue_runs() -> void:
	# The sabotages that need a driven lap. `hint` needs none — it walks the line
	# through `Track::project` and never builds a scene — and `ceiling` needs the
	# driven lap but takes its sabotage in `_case_limits`.
	if _break in ["steer", "noline", "nocourse", "brake", "lookahead", "ceiling"]:
		if _break == "ceiling" and _ceiling_lateral_g <= 0.0:
			# `_case_limits` did not run, so there is nothing to quarter and the
			# control would be inert. That is the `replay.sh --break=input`
			# defect — a control whose default value cannot fire.
			_fail("break=ceiling has a ceiling to sabotage",
					"run it with --case=all so _case_limits() fills one")
			return
		_pending.append({"label": "break", "layout": "forward", "grip": 0.98, "laps": 1})
		return
	if _wants("wiring"):
		_pending.append({"label": "wiring", "layout": "forward", "grip": 0.98, "laps": 1})
	if _wants("lap"):
		_pending.append({"label": "lap", "layout": "forward", "grip": 0.98, "laps": LAPS})
	if _wants("reverse"):
		_pending.append({"label": "reverse", "layout": "reverse", "grip": 0.98, "laps": 1})
	if _wants("tiers"):
		for usage in [1.0, 0.85, 0.70]:
			_pending.append({
				"label": "tier %.2f" % usage, "layout": "forward", "grip": usage, "laps": 1,
			})


func _wants(name: String) -> bool:
	if _break != "":
		return false
	return _case == "all" or _case == name


## Which check each sabotage has to turn red, by substring.
##
## **The verdict demands the saboteur's own fingerprint.** Without this a
## `--break` run passes on any red at all, including one the sabotage did not
## cause — and a suite whose controls are satisfied by a pre-existing failure is
## a suite that reports "caught" while the gate is broken. `shell_probe.gd`'s
## first cut did exactly that.
const BREAK_FINGERPRINT := {
	"steer": "stayed on its own line",
	"noline": "completed its laps",
	"nocourse": "completed its laps",
	"brake": "friction ellipse",
	"lookahead": "stayed on its own line",
	"ceiling": "friction ellipse",
	"hint": "agrees with an unhinted one",
}

var _reds := PackedStringArray()


func _fail(what: String, detail: String) -> void:
	printerr("  FAIL  %s: %s" % [what, detail])
	_failures += 1
	_checks += 1
	_reds.append(what)


func _record(what: String, ok: bool, detail: String) -> void:
	_checks += 1
	if ok:
		print("  pass  %s: %s" % [what, detail])
	else:
		printerr("  FAIL  %s: %s" % [what, detail])
		_failures += 1
		_reds.append(what)


var _done := false


func _finish() -> void:
	# `quit()` schedules; it does not return. So `_initialize`'s call and the
	# first `_process` both reach here and the verdict is printed twice — which
	# is cosmetic until somebody greps the output for a pass count.
	if _done:
		return
	_done = true
	_scrub()
	for note in _notes:
		print("  note  " + note)
	if _break != "":
		# **Inverted, and fingerprinted.** A sabotage that goes unnoticed is the
		# failure — and so is one that is "caught" by a red it did not cause.
		var wanted := String(BREAK_FINGERPRINT.get(_break, ""))
		if wanted == "":
			printerr("== ai probe: --break=%s is not a mode; see BREAK_FINGERPRINT ==" % _break)
			quit(1)
			return
		var matched := PackedStringArray()
		for red in _reds:
			if red.contains(wanted):
				matched.append(red)
		if matched.is_empty():
			printerr("== ai probe: --break=%s went UNNOTICED: %d red of %d checks, none of them "
					% [_break, _failures, _checks]
					+ "'%s' ==" % wanted)
			for red in _reds:
				printerr("     red, but not the one this sabotage causes: %s" % red)
			quit(1)
			return
		print("== ai probe: --break=%s caught by %d check(s) naming '%s' (%d red of %d) =="
			% [_break, matched.size(), wanted, _failures, _checks])
		quit(0)
		return
	if _failures == 0:
		print("== ai probe: pass (%d checks) ==" % _checks)
		quit(0)
	else:
		printerr("== ai probe: %d failure(s) of %d checks ==" % [_failures, _checks])
		quit(1)


# --- the track copy ----------------------------------------------------------


## Copy the circuit under a slug nothing else uses. Byte for byte: `KartTrack`
## hashes the file's own bytes and a reformat would change the content hash the
## session is filed against.
func _copy_track() -> bool:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(PROBE_DIR))
	DirAccess.make_dir_recursive_absolute(PROBE_DIR)
	var source := FileAccess.get_file_as_bytes(TRACK_SOURCE)
	if source.is_empty():
		_fail("the circuit file is there", "no %s" % TRACK_SOURCE)
		return false
	var target := FileAccess.open(TRACK_COPY, FileAccess.WRITE)
	if target == null:
		_fail("the circuit copy is written", "could not open %s" % TRACK_COPY)
		return false
	target.store_buffer(source)
	target.close()

	_track = KartTrack.new()
	if _track.load(TRACK_COPY) != OK or not _track.is_loaded():
		_fail("the circuit copy loads", "; ".join(_track.problems()))
		return false
	return true


## Delete everything this probe wrote. The ghost matters most: `KartGhost` has no
## `set_base_dir()`, so a run that left one behind would leave it in the real
## `user://ghosts` under a slug that at least cannot collide with a real one.
func _scrub() -> void:
	for path in [TRACK_COPY]:
		if FileAccess.file_exists(path):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	var ghosts := DirAccess.open("user://ghosts")
	if ghosts != null:
		for name in ghosts.get_files():
			if name.begins_with(TRACK_SLUG):
				ghosts.remove(name)


# --- the model ---------------------------------------------------------------


## What the AI was told it could do, read at run time.
##
## Printed rather than restated. When issue #137 lands these numbers move and
## nothing in this file has to be edited, which is the whole design of the gate.
func _case_limits(report: bool) -> void:
	if report:
		print("-- limits, read from KartRacingLine.model() at run time")
	var line := KartRacingLine.new()
	if not line.build_from_course(_track, 1.5):
		_fail("the line solves on the circuit", "build_from_course refused")
		return
	var model: Dictionary = line.model()
	var summary: Dictionary = line.summary()
	_ceiling_lateral_g = minf(model["lateral_left_g"], model["lateral_right_g"])
	if _break == "ceiling":
		# The sabotage: a ceiling a quarter of the real one, which is what a
		# hardcoded figure becomes on the day the tire gets worse. The
		# utilization gate must go red — a gate that stayed green would be one
		# that is not really dividing by the number it prints.
		#
		# **Deliberately the low direction and not the high one.** Multiplying
		# the ceiling makes every utilization read *low* and the gate green,
		# which proves nothing: a control that cannot turn a check red is not a
		# control.
		_ceiling_lateral_g *= 0.25
		_notes.append("break=ceiling: the lateral ceiling was quartered")
	if not report:
		return
	print("   lateral ceiling  %.4f g left, %.4f g right   (tire %.4f / %.4f, rollover %.4f / %.4f)"
		% [model["lateral_left_g"], model["lateral_right_g"], model["tire_left_g"],
			model["tire_right_g"], model["rollover_left_g"], model["rollover_right_g"]])
	print("   brake ceiling    %.4f g        traction %.4f g   peak friction %.3f"
		% [model["brake_g"], model["traction_g"], model["peak_friction"]])
	print("   line             %.1f m, %d stations, %.2f-%.2f km/h, %d braking zones, %d shifts"
		% [summary["line_length"], summary["stations"], summary["min_speed_kmh"],
			summary["max_speed_kmh"], summary["braking_zones"], summary["shifts"]])
	print("   line lap time    %.3f s  (reported, never gated)" % summary["lap_time"])

	_record("the model answered with a lateral ceiling",
			model["lateral_left_g"] > 0.0 and model["lateral_right_g"] > 0.0,
			"%.4f g / %.4f g, whichever the tire and the geometry allow today"
					% [model["lateral_left_g"], model["lateral_right_g"]])
	_record("the model answered with a brake ceiling", model["brake_g"] > 0.0,
			"%.4f g" % model["brake_g"])
	# The relation that says the AI is reading the same model the line was solved
	# with rather than a second copy of it.
	_record("the line's own profile stays inside the ellipse",
			line.worst_combined_utilization() <= 1.0 + 1e-4,
			"worst combined utilization %.6f, worst lateral %.6f"
					% [line.worst_combined_utilization(), line.worst_lateral_utilization()])


# --- the hint ----------------------------------------------------------------


## Does a carried station hint agree with an unhinted walk?
##
## `Track::project`'s search window could exclude its own hint until `b783c61`: a
## carried-hint walk of Valdirone ended **687.5 m** wrong, every lap was struck
## `off_track`, and no lap could ever be filed. The AI hints every tick, so it is
## on that exact path. This walks the *line* — which is where the AI actually
## drives, not the centerline — and compares the two answers station by station.
##
## Walking the line rather than the centerline is the point. A centerline walk
## projects a point onto the curve it came from and the answer is trivially
## itself; the AI is up to 3 m off it, which is where a windowed search can pick
## the wrong span.
func _case_hint() -> void:
	print("-- the carried station hint, against an unhinted walk")
	var line := KartRacingLine.new()
	if not line.build_from_course(_track, 1.5):
		_fail("the line solves on the circuit", "build_from_course refused")
		return
	var points: PackedVector3Array = line.points()
	var worst := 0.0
	var worst_at := 0.0
	var hint := -1.0
	var count := points.size() - 1
	# Two laps of it, because the defect showed up as drift that accumulated.
	for pass_index in 2:
		for index in count:
			var here := points[index]
			var hinted: Dictionary = _track.project(here, hint)
			var unhinted: Dictionary = _track.project(here, -1.0)
			hint = hinted["distance"]
			if _break == "hint":
				# The sabotage: carry a hint that is a fixed distance behind, so
				# the window trails the kart. If the comparison is real this
				# diverges; if it is not, it stays quiet.
				hint = fmod(hint + _track.length() * 0.5, _track.length())
			var gap: float = absf(hinted["distance"] - unhinted["distance"])
			# The short way round the lap, so a station near the line is not a
			# whole-lap disagreement.
			gap = minf(gap, _track.length() - gap)
			if gap > worst:
				worst = gap
				worst_at = unhinted["distance"]
	_record("a hinted walk of the racing line agrees with an unhinted one",
			worst < 0.5,
			"worst disagreement %.4f m at station %.1f m over 2 laps of %d stations"
					% [worst, worst_at, count])


# --- the driven runs ---------------------------------------------------------


const PH_IDLE := 0
const PH_LOAD := 1
const PH_ATTACH := 2
const PH_DRIVE := 3
const PH_REPORT := 4


func _process(_delta: float) -> bool:
	match _phase:
		PH_IDLE:
			if _pending.is_empty():
				_finish()
				return true
			_start_run(_pending.pop_front())
		PH_LOAD:
			_phase_load()
		PH_ATTACH:
			_phase_attach()
		PH_REPORT:
			_teardown()
	_phase_frame += 1
	return false


func _physics_process(_delta: float) -> bool:
	if _phase == PH_DRIVE:
		_tick()
	return false


func _advance(next: int) -> void:
	_phase = next
	_phase_frame = 0


func _start_run(run: Dictionary) -> void:
	_run_label = run["label"]
	_run_layout = run["layout"]
	_run_grip = run["grip"]
	_run_laps = run["laps"]
	print("-- %s: %s layout, grip usage %.2f, %d lap%s"
		% [_run_label, _run_layout, _run_grip, _run_laps, "" if _run_laps == 1 else "s"])

	_laps = []
	_worst_lateral = 0.0
	_worst_combined = 0.0
	_worst_lateral_at = 0.0
	_max_slip = 0.0
	_max_cross = 0.0
	_min_speed = 1e30
	_max_speed = 0.0
	_shifts = 0
	_last_gear = 0
	_off_grid = 0
	_ticks = 0
	_stall_ticks = 0
	_last_odometer = 0.0
	_stalled = false

	# The command line the scene is built from. `--profile-dir` keeps every
	# `KartProfile` and `KartSettings` this run touches inside `user://ai_probe/`;
	# without it a probe in a worktree overwrites the real career, because every
	# worktree shares one `user://`.
	var arguments := {
		"track": TRACK_COPY,
		"layout": _run_layout,
		"ai-solo": "true",
		"ai-grip": "%f" % _run_grip,
		# **One more than asked for, because the first crossing is an out lap.**
		# `SessionRunner._limit_met()` counts start-line crossings and says so in
		# its own words; `KartTrack.grid_transform(0)` puts the kart at station
		# 1373.8 of 1375.1, which is **1.3 m** behind the line. So a one-lap
		# session ends 1.675 s after the green with "no timed lap" — measured,
		# and it looked exactly like an AI that had parked itself.
		"laps": "%d" % (_run_laps + 1),
		"session": "practice",
		"profile-dir": PROBE_DIR,
		"hud": "false",
		"mesh": "false",
		"scatter": "false",
		"terrain": "false",
		"runoff": "false",
		"ghost": "off",
		"camera": "chase",
	}
	# **Posted, not appended to the command line.** `SessionRequest.take()` merges
	# the posted keys first and the command line's second, so *the command line
	# wins every key it carries* — and this probe's own command line carries
	# `--case=` and `--break=`, which the scene ignores. Passing the run
	# configuration the other way round would have let a stray `--layout=` on the
	# probe's own invocation silently override the run.
	#
	# **No sabotage is posted here and none reaches `circuit.gd`.** Every negative
	# control is applied to the live `AiDriver` in `_sabotage()` after the scene
	# is up. A `--break` hook in production code is a branch that ships, and the
	# thing being tested then is the hook.
	SessionRequest.post(arguments)
	_advance(PH_LOAD)


func _phase_load() -> void:
	# **Keyed on `_circuit` being null, not on `_phase_frame == 0`.** `_advance()`
	# zeroes the counter and the *same* `_process` call then increments it, so
	# frame 0 of a phase entered from another phase never happens — the scene was
	# never instantiated, `_phase_attach` dereferenced null, and the run printed
	# 98,951 identical script errors at 3,900 a second while looking like a hang.
	# A state test cannot go wrong that way; a frame count can.
	if _circuit == null:
		var packed := load(CIRCUIT_SCENE) as PackedScene
		if packed == null:
			_fail("the circuit scene loads", CIRCUIT_SCENE)
			_advance(PH_IDLE)
			return
		_circuit = packed.instantiate()
		root.add_child(_circuit)
		return
	# **Not in `_initialize`, and not on the frame the node was added.** A
	# `--script` main loop's scene is not in the tree during `_initialize` and
	# `_ready` has not run on the frame it is parented, so the nodes it builds do
	# not exist yet. `shoot.gd` and `drive_probe.gd` both look up on the first
	# `_physics_process` for the same reason.
	if _phase_frame >= 2:
		_advance(PH_ATTACH)


func _phase_attach() -> void:
	if _circuit == null:
		_fail("%s: the circuit scene was instantiated" % _run_label,
				"nothing was built; see _phase_load")
		_advance(PH_REPORT)
		return
	_runner = _circuit.get_node_or_null("SessionRunner") as SessionRunner
	_kart = _circuit.find_child("Kart", true, false) as KartBody
	_ai = _circuit.find_child("AiDriver", true, false) as AiDriver
	if _runner == null or _kart == null or _ai == null:
		_fail("%s: the scene came up with an AI and a session" % _run_label,
				"runner %s, kart %s, ai %s" % [_runner, _kart, _ai])
		_advance(PH_REPORT)
		return
	if _runner.state() == SessionRunner.STATE_REFUSED:
		_fail("%s: the session started" % _run_label, _runner.refusal())
		_advance(PH_REPORT)
		return

	if _run_label == "wiring":
		_check_wiring()

	if _trace:
		var session: KartSession = _runner.get("_session")
		print("     session limit kind %d value %.1f  auto_shift %s auto_clutch %s"
			% [session.get_limit_kind(), session.get_limit_value(),
				_kart.auto_shift, _kart.auto_clutch])
		print("     ai limits %s" % _ai.limits())
		# Where the kart is put down against where the line wants it. A grid slot
		# is not on the racing line by construction — the line is a lap-optimal
		# curve and the grid is a regulation box — so the first corner is always
		# a rejoin, and how big a rejoin is worth knowing before reading a
		# cross-track number.
		var placed: Dictionary = _track.project(_kart.global_position, -1.0)
		var frame: Dictionary = _track.sample(placed["distance"])
		print("     grid at station %.1f m, %.2f m lateral of the centerline, road %.1f m wide"
			% [placed["distance"], placed["lateral"], frame["width"]])

	_sabotage()
	_stale_at_start = _kart.get_stale_input_ticks()
	_runner.lap_completed.connect(_on_lap_completed)
	_advance(PH_DRIVE)


## The negative controls, applied to the live node.
##
## Each one has to produce a red in a **named** check, and `_finish()` demands
## exactly that. The first cut of `shell_probe.gd`'s `--break` reported "caught"
## off a pre-existing red it had not caused, which is a negative control that
## proves the gate was already broken rather than that it works.
func _sabotage() -> void:
	match _break:
		"steer":
			# The steering answer, negated. Pure pursuit then drives away from
			# the line at exactly the rate it should be driving toward it — the
			# failure a flipped basis would produce, and the reason
			# `test_ai_driver.cpp` asserts the sign against `vehicle_state.h`'s
			# own sentence rather than against a remembered convention.
			_ai.steer_gain = -1.0
		"noline":
			# Nothing to follow. It must push neutral and file no lap, not coast
			# round on a stale snapshot.
			_ai.line = null
		"nocourse":
			# Nothing to project against. Same requirement, different half.
			_ai.set_course(null)
		"brake":
			# Plan against four times the brake the model says is there. Every
			# brake point then lands far too late and the kart arrives at the
			# corner carrying speed it cannot turn with.
			_ai.brake_plan_fraction = 4.0
		"lookahead":
			# A lookahead of centimeters. Pure pursuit's circle through a point
			# that close is unbounded, so the steering saturates and chatters —
			# the classic way this controller fails and the one a fixed
			# lookahead would have shipped with.
			_ai.lookahead_base = 0.1
			_ai.lookahead_gain = 0.0


## Is this an ADR-0040 producer, or something that merely works?
##
## Four separate questions, and the third is the one that has been silently wrong
## before: a producer that runs *after* the body leaves the vehicle on last
## tick's intent, `KartBody` falls to neutral, and the whole thing reads as a
## physics bug rather than as an ordering bug.
func _check_wiring() -> void:
	_record("the AI node is a PlayerDriver",
			_ai is PlayerDriver,
			"AiDriver derives from PlayerDriver, so SessionRunner.configure()'s "
			+ "statically typed driver argument accepts it and the runner is the "
			+ "single writer of `enabled` for an AI exactly as for a human")
	_record("it runs before the body",
			_ai.get_physics_process_priority() < _kart.get_physics_process_priority(),
			"driver priority %d, body priority %d"
					% [_ai.get_physics_process_priority(),
						_kart.get_physics_process_priority()])
	# `_ready` is the half that godot-cpp will silently not register for a
	# derived class, and the priority above is what `_ready` sets. So that check
	# is also the check that the virtual was re-declared.
	var limits: Dictionary = _ai.limits()
	_record("it was told the kart's ceilings rather than carrying them",
			bool(limits["complete"]),
			"brake %.4f g, lateral %.4f g, wheelbase %.4f m, lock %.4f rad — every one "
			% [limits["brake_limit_g"], limits["lateral_limit_g"], limits["wheelbase_m"],
				limits["max_lock_rad"]]
			+ "read from KartRacingLine.model() and KartBody at _ready")
	_record("the kart has no scripted input driver overriding the push",
			not _kart.input_driver.is_valid(),
			"KartBody.input_driver is empty, so what the solver consumes is what "
			+ "AiDriver pushed — a valid Callable wins over pushed input, which is "
			+ "how four drive.sh scenarios once agreed on one hash while measuring nothing")


func _on_lap_completed(number: int, time_s: float, valid: bool, reason: String) -> void:
	_laps.append({"number": number, "time": time_s, "valid": valid, "reason": reason})


func _tick() -> void:
	_ticks += 1
	if _kart == null or _runner == null or _ai == null:
		_advance(PH_REPORT)
		return

	var speed := _kart.get_speed_ms()
	_min_speed = minf(_min_speed, speed)
	_max_speed = maxf(_max_speed, speed)

	# **Indexed, not `get(key, default)`.** These are a contract. A renamed key
	# read with a default does not fail loudly — it draws a zero forever, which is
	# how the front wheels sat dead straight through every corner for a milestone.
	var decision: Dictionary = _ai.decision()
	var throttle: float = decision["throttle"]
	var brake: float = decision["brake"]
	var steer: float = decision["steer"]
	var clutch: float = decision["clutch"]

	# Every value the AI pushes has to be on the replay storage grid, because
	# `replay_encode_input` refuses off-grid input rather than rounding it. A
	# producer that cannot be recorded is a producer whose laps cannot be saved,
	# and the failure is a flat refusal rather than a subtle drift.
	if not _on_grid(throttle) or not _on_grid(brake) or not _on_grid(clutch) \
			or not _on_steer_grid(steer):
		_off_grid += 1

	if _kart.get_gear() != _last_gear and _last_gear != 0 and _kart.get_gear() != 0:
		_shifts += 1
	_last_gear = _kart.get_gear()

	# The ellipse, measured on the kart rather than on the line. `lateral_g` and
	# `longitudinal_g` are the solver's own, computed from the tick's applied
	# forces.
	var lateral: float = absf(_kart.get_lateral_g())
	var longitudinal: float = absf(_kart.get_longitudinal_g())
	if _ceiling_lateral_g > 0.0:
		var lateral_use := lateral / _ceiling_lateral_g
		if lateral_use > _worst_lateral:
			_worst_lateral = lateral_use
			_worst_lateral_at = _ai.get_station_m()
		var brake_ceiling: float = _ai.limits()["brake_limit_g"]
		if brake_ceiling > 0.0:
			var combined := sqrt(
				lateral_use * lateral_use
				+ (longitudinal / brake_ceiling) * (longitudinal / brake_ceiling)
			)
			_worst_combined = maxf(_worst_combined, combined)

	_max_slip = maxf(_max_slip, _ai.get_max_body_slip_rad())
	_max_cross = maxf(_max_cross, _ai.get_max_cross_track_m())

	# Forward progress, so a run that has parked itself ends in ten seconds of
	# sim rather than in four minutes of wall clock. Measured on the odometer
	# because it is monotone; a station goes round the lap and a speed can be
	# briefly zero at a legitimate standing start.
	var odometer := _runner.odometer_m()
	if odometer > _last_odometer + 0.5:
		_last_odometer = odometer
		_stall_ticks = 0
	else:
		_stall_ticks += 1

	if _trace and _ticks < 900 and _ticks % 60 == 0:
		print("     T%-5d kart (%.1f, %.1f) h %.3f  line (%.1f, %.1f)  aim (%.1f, %.1f)  "
			% [_ticks, decision["kart_x"], decision["kart_z"], decision["heading"],
				decision["line_x"], decision["line_z"], decision["aim_x"], decision["aim_z"]]
			+ "str %+.3f xt %+.2f v %.1f" % [steer, decision["cross_track"], speed])
	if _trace and _ticks % TRACE_EVERY == 0:
		print("     t%-6d  %6.1f m  %6.2f km/h  gear %d  thr %.2f brk %.2f str %+.3f  "
			% [_ticks, _ai.get_station_m(), speed * 3.6, _kart.get_gear(),
				throttle, brake, steer]
			+ "xtrack %+.2f m  want %.1f km/h"
			% [_ai.get_cross_track_m(), _ai.get_target_speed_ms() * 3.6])

	if _runner.is_over():
		_advance(PH_REPORT)
	elif _stall_ticks > STALL_TICKS:
		_stalled = true
		_advance(PH_REPORT)
	elif _ticks > TICK_BUDGET:
		_advance(PH_REPORT)


## `replay.h`'s own constants, and getting them wrong is how this check first
## reported 1,247 of 2,042 ticks off grid against a driver that was snapping
## correctly. `REPLAY_UNIT_CODES` is **65535** and `REPLAY_STEER_CODES` is
## **32767**; the first cut of this probe assumed 1023 and 2047 and produced a
## confident red naming the wrong file.
const REPLAY_UNIT_CODES := 65535.0
const REPLAY_STEER_CODES := 32767.0


func _on_grid(value: float) -> bool:
	# Compared with a tolerance a float round-trip cannot exceed, not with `==`.
	return absf(value * REPLAY_UNIT_CODES - roundf(value * REPLAY_UNIT_CODES)) < 1e-6


func _on_steer_grid(value: float) -> bool:
	return absf(value * REPLAY_STEER_CODES - roundf(value * REPLAY_STEER_CODES)) < 1e-6


func _teardown() -> void:
	_report_run()
	if _circuit != null:
		_circuit.queue_free()
		_circuit = null
	_runner = null
	_kart = null
	_ai = null
	_advance(PH_IDLE)


func _report_run() -> void:
	var valid := 0
	var struck := 0
	var best := 0.0
	for lap in _laps:
		if lap["valid"]:
			valid += 1
			if best == 0.0 or lap["time"] < best:
				best = lap["time"]
		else:
			struck += 1
	print("   %d lap(s): %d valid, %d struck; best %s; %.1f-%.1f km/h; %d shifts; %d ticks"
		% [_laps.size(), valid, struck,
			"none" if best == 0.0 else SessionRunner.format_time(best),
			_min_speed * 3.6 if _min_speed < 1e29 else 0.0, _max_speed * 3.6,
			_shifts, _ticks])
	for lap in _laps:
		print("     lap %d  %s  %s%s" % [
			lap["number"], SessionRunner.format_time(lap["time"]),
			"valid" if lap["valid"] else "STRUCK",
			"" if String(lap["reason"]) == "" else "  (" + String(lap["reason"]) + ")",
		])
	print("   utilization: lateral %.4f of the run-time ceiling at station %.0f m, combined %.4f"
		% [_worst_lateral, _worst_lateral_at, _worst_combined])
	print("   max body slip %.2f deg   max cross-track %.3f m"
		% [rad_to_deg(_max_slip), _max_cross])

	if _run_label == "break":
		_gate_run(valid, struck, best)
		_tier_results.append({"grip": _run_grip, "best": best, "valid": valid})
		return
	_gate_run(valid, struck, best)
	if _run_label.begins_with("tier"):
		_tier_results.append({"grip": _run_grip, "best": best, "valid": valid})
		if _tier_results.size() == 3:
			_check_tiers()


## The gates. Every one of them is a relation against something read at run time.
func _gate_run(valid: int, struck: int, best: float) -> void:
	var label := _run_label
	_record("%s: the AI completed its laps" % label, valid >= 1,
			"%d valid of %d, %d struck out; %d ticks of a %d budget"
					% [valid, _laps.size(), struck, _ticks, TICK_BUDGET])
	_record("%s: no lap was struck for track limits" % label, struck == 0,
			"%d struck" % struck if struck > 0 else "every completed lap counted")
	_record("%s: the kart stayed inside the friction ellipse" % label,
			_worst_lateral <= UTILIZATION_CEILING and _worst_combined <= UTILIZATION_CEILING,
			"worst lateral %.4f, worst combined %.4f, ceiling %.2f of a lateral limit of "
			% [_worst_lateral, _worst_combined, UTILIZATION_CEILING]
			+ "%.4f g read at run time" % _ceiling_lateral_g)
	_record("%s: it did not depart" % label, _max_slip < DEPARTURE_RAD,
			"max body slip %.2f deg against the %.2f deg departure band"
					% [rad_to_deg(_max_slip), rad_to_deg(DEPARTURE_RAD)])
	_record("%s: it stayed on its own line" % label, _max_cross <= CROSS_TRACK_CEILING,
			"max cross-track error %.3f m against a %.2f m corridor"
					% [_max_cross, CROSS_TRACK_CEILING])
	_record("%s: every input it pushed was on the replay grid" % label, _off_grid == 0,
			"%d of %d ticks off grid" % [_off_grid, _ticks] if _off_grid > 0
					else "%d ticks, all of them recordable" % _ticks)
	if best > 0.0:
		_notes.append("%s best lap %s" % [label, SessionRunner.format_time(best)])


## A lower tier is never faster. The only assertion this file makes about a lap
## time and it is an ordering, not a number.
func _check_tiers() -> void:
	print("-- difficulty tiers")
	var ok := true
	var rows := PackedStringArray()
	for index in _tier_results.size():
		var row: Dictionary = _tier_results[index]
		rows.append("grip %.2f -> %s" % [
			row["grip"],
			"no lap" if float(row["best"]) == 0.0 else SessionRunner.format_time(row["best"]),
		])
		if index > 0:
			var previous: Dictionary = _tier_results[index - 1]
			if float(row["best"]) <= 0.0 or float(previous["best"]) <= 0.0:
				ok = false
			elif float(row["best"]) < float(previous["best"]):
				ok = false
	_record("a lower difficulty tier is never faster", ok, ", ".join(rows))
