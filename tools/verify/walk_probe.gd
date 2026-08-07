extends SceneTree

## The walk: boot to a filed lap time, once, through everything.
##
##     godot --headless --fixed-fps 120 --path . --script tools/verify/walk_probe.gd
##       --case=<name>[,<name>]   run a subset; default is all of them
##       --break=<mode>           sabotage one property and assert the gate
##                                catches it. **Inverted exit** — see below
##       --list-breaks            print the mode names, one per line, and exit
##       --patch=<a,b>            apply a named workaround for a product defect
##                                so the checks downstream of it can be measured.
##                                Default is every patch the run finds it needs;
##                                `--patch=none` drives the unpatched walk
##       --keep                   leave user://walk_probe/ behind
##       --trace=600              a trace line every N physics ticks
##
## `tools/verify/walk.sh` is the wrapper: ADR-0018's double import, then this,
## then every `--break` mode as the negative-control pass.
##
## ## Why this file exists
##
## Every piece of the M5f walk is gated in isolation and the walk itself has
## never run. `shell_probe.gd` drives the screens and hands the results sheet
## twelve synthetic laps; `best_lap_store` is measured against Dictionaries a
## probe wrote; `lap_ledger.gd` was measured against a synthetic timer; the ghost
## is measured against analytic geometry; the shell-to-session hand-off is
## measured as a *string set*. Not one of those touches the next one. This drives
## the join, end to end, once:
##
##   1. boot -> paddock -> setup, **pad-only**, through the real `ScreenStack`
##   2. the setup sheet's own `config()` into `ShellRoot.start_session()`
##   3. a complete timed lap of the circuit, driven
##   4. the lap ledger has a row for it and the row's sectors sum to the lap
##   5. **the pause strike, for real** — see below
##   6. out through the pause menu; the results sheet pins the lap that was driven
##   7. `KartProfile` on disk holds it and a *fresh* one reads it back
##   8. the ghost this session recorded is on disk and loads
##
## ## The pause strike is the check M5f could not make
##
## `circuit.gd:_strike_paused_lap()` calls `KartLapTimer::strike_paused()` and
## nothing had ever proved the call arrives, because three things make it
## unobservable from a parked kart: a kart on the grid is on its **out lap**
## forever and a strike on an out lap is correctly a no-op; `taint()` is
## first-reason-wins, so a lap already off-track swallows it; and the runner
## overwrites any injected distance 120 times a second. A probe that drives real
## laps has none of those problems, so this one pauses in the middle of a genuine
## timed lap, twice — once with `pause_invalidates_lap` off and once with it on —
## and reads `current_reason()` on both sides of the press.
##
## ## Three rules this probe obeys, and every one of them is a trap already paid for
##
## **Every worktree shares one `user://`.** All profile and settings I/O goes
## under `user://walk_probe/` through `--profile-dir=`, which `ShellRoot` honors.
## `KartGhost` has **no** `set_base_dir()` — `ghost_directory()` is a hardcoded
## `user://ghosts` and the id is derived from track + layout + class — so the
## circuit is driven from a **copy of the track file under a slug that cannot
## collide**, `walkprobe_circuit`, and `_scrub()` deletes what it wrote. Driving
## `valdirone_nuova` here would overwrite the runner's own personal-best ghost.
##
## **`Input.parse_input_event()` buffers; it does not dispatch.** Accumulation is
## turned off and the queue is flushed by hand after every send, or every
## assertion reads the press before it and the last one lands a frame late on
## whatever screen exists by then.
##
## **Headless the root viewport is 1600x1600.** Pinned to the design canvas, the
## same way `shell_probe.gd` does it, so anything measured about a control's rect
## is measured against the shipped canvas.
##
## ## `--patch`, and why a gate ships with workarounds in it
##
## This walk found three product defects that stop it dead, all of them outside
## this file. A gate that simply stopped at the first one would ship six hundred
## lines of check code that has never executed — which is the same defect as a
## check that cannot fail, one level up. So each blocker has:
##
##   * a **check that measures it**, runs first, needs no scene where possible,
##     and is RED until the product is fixed; and
##   * a named **patch** that works around it *in the probe only*, applied only
##     when the defect is actually measured to be present.
##
## The gate is therefore red for as long as the defects exist and green the day
## they are fixed, with no edit here: a patch whose defect has gone is reported as
## unnecessary rather than applied. `--patch=none` drives the unpatched walk,
## which is what a driver actually gets today.
##
## ## Why this is not `session_probe.gd`
##
## That file drives `test_track.tscn` — a diagnostic instrument built in code — to
## measure the timer, the projection and the state machine. It never touches the
## shell, never saves anything, and never opens a menu. This one measures nothing
## about the vehicle and everything about the joins between the shell, the
## session, the profile and the disk. The pace driver below is deliberately the
## same shape as that file's, and quotes the same measured corner-speed table,
## because a second opinion on how to get round a corner would be a second thing
## to debug.

# --- what it drives ---------------------------------------------------------

const SHELL_SCENE := "res://scenes/shell/shell.tscn"
const TRACK_SOURCE := "res://data/tracks/valdirone_nuova.track.json"

const BASE_DIR := "user://walk_probe/"

## The circuit is driven from a **copy** under this slug. See the header: the
## ghost id is derived from the track slug and `KartGhost` cannot be redirected,
## so a probe that drove `valdirone_nuova` would overwrite a real ghost. The name
## is `profile_is_slug`-legal (lower case and underscores, like the real one) and
## exists nowhere else in the project.
const TRACK_SLUG := "walkprobe_circuit"
const TRACK_COPY := BASE_DIR + TRACK_SLUG + ".track.json"

## The design canvas, from `project.godot`. Headless the window collapses to its
## 64x64 minimum and `stretch/aspect="expand"` squares the canvas off against it,
## so anything measured about a `Control`'s rect is measured 700 px too tall.
const VIEWPORT := Vector2i(1600, 900)

## Every case, in the order they run. `--case=` picks a subset, and **a case name
## that gates a check gates every `_record` that check makes** — a subset run that
## quietly measures something else prints a number that looks like an answer.
const CASES: PackedStringArray = [
	"blockers", "shell", "handoff", "lap", "ledger", "strike",
	"results", "profile", "ghost",
]

## Which cases need the walk driven at all. Kept in one place because a case added
## to `CASES` and forgotten here measures nothing and reports a crash.
const DRIVEN_CASES: PackedStringArray = [
	"shell", "handoff", "lap", "ledger", "strike", "results", "profile", "ghost",
]

## `--break=<mode>` to `[check name prefix, evidence]`, `shell_probe.gd`'s table
## and its reasoning: **a check that was already failing for its own reasons turns
## any sabotage aimed at it into a green light**, so where the sabotage leaves a
## fingerprint in the measurement the verdict demands it.
##
## That guard matters more here than it does there, because this gate has three
## checks that are red in a clean tree on purpose.
## Each `evidence` is something the check has to have **measured**, not a marker
## the probe printed about itself:
##
##   nolap    the profile's own count did not move
##   sectors  a row exists and its splits do not add up to its time
##   strike   the reason was read on both sides of the press and did not move
##   ghost    the profile still names a ghost that is not on the disk
const BREAK_MODES := {
	"nolap": ["the profile on disk holds the lap", "best_count 0 -> 0"],
	"sectors": ["the ledger row's sectors sum to the lap", "the disagreement is +"],
	"strike": ["pausing strikes the lap when the setting is on", "valid -> valid"],
	"ghost": ["the session's ghost is on disk and loads", "points at walkprobe_circu"],
}

## The patches, mode to the check whose red is what justifies it. Applied only
## when that check actually goes red, so a fixed product silently stops patching.
const PATCHES := {
	"hint": "the hinted projection carries forward",
	"ledger": "circuit.gd attaches a LapLedger",
}

# --- the pace driver ---------------------------------------------------------
#
# `session_probe.gd`'s controller, against a `KartTrack` instead of a
# `TrackLayout`. Its header's argument holds unchanged: a lap of a real circuit
# is not reachable by any fixed function of a tick counter, so the driver is
# closed loop, this probe publishes no state hash, and it must never become a
# determinism gate. `tools/verify/drive.sh` is that gate and is untouched by
# anything here.

## The measured corner-speed table, from `drive_probe.gd`'s recorded steering-lock
## sweep against `KartBody`. Quoted rather than derived for the reason
## `session_probe.gd` gives: deriving a corner speed needs the tire's peak
## friction, the Ackermann solver and the load transfer, which is the whole
## vehicle model.
##
##     radius m   settles at km/h
##       58.49        116.9
##       50.95        108.5
##       32.64         81.3
##       18.99         46.0
##        3.49         21.2
##        2.70          5.6
const CORNER_RADIUS_M: PackedFloat64Array = [2.70, 3.49, 18.99, 32.64, 50.95, 58.49]
const CORNER_SPEED_KMH: PackedFloat64Array = [5.6, 21.2, 46.0, 81.3, 108.5, 116.9]

const LOOK_SECONDS := 0.55
const LOOK_MIN_M := 6.0
const LOOK_MAX_M := 26.0
const CORNER_LOOKAHEAD_M := 70.0
const STEER_GAIN_HEADING := 1.6
const STEER_GAIN_LATERAL := 0.09
const SPEED_BAND_MS := 1.2

## How finely the radius table is walked, meters. 2 m over a 1,375 m lap is 687
## rows and resolves Il Pozzo's 15 m radius to a tenth of its own arc.
const RADIUS_STEP_M := 2.0

# --- the script the walk follows ---------------------------------------------

## Crossings to drive. One out lap from the grid, then two timed laps: the first
## is paused with the setting **off** and must survive to become the session best,
## the second is paused with it **on** and must be struck out.
##
## Two rather than three because the forgiving lap is also the clean one — there
## is nothing a third lap would measure that these two do not, and each one is
## about 7,000 ticks.
const CROSSINGS_WANTED := 3

## Where in the lap the pause happens, as a fraction of the circuit. Comfortably
## past the first sector mark at 524 m of 1,375 m (0.38) and comfortably before
## the second at 902 m (0.66), so the strike lands in the middle of a sector and
## neither episode can be confused with a mark crossing.
const PAUSE_AT_FRACTION := 0.50

## Frames the pause menu is held open. `ScreenStack` lands focus — and calls
## `on_enter()`, which is what gates the input and fires the strike — on the frame
## *after* the push, so anything measured on the push frame reads the state before
## it. Four is that with margin and is still 33 ms of a lap.
const PAUSE_HOLD_FRAMES := 4

## The watchdog. Three crossings measured at 14,500 ticks with this controller;
## 40,000 is 2.7 times that and about 14 s at the rate `--fixed-fps 120` runs
## headless. A gate that hangs is worse than a gate that is wrong.
const MAX_TICKS := 40000

## And the same for the parts that are counted in frames rather than ticks — the
## menu walk, the two scene swaps. Generous, because `start_session()` awaits two
## process frames on purpose and `change_scene_to_file` is deferred.
const MAX_PHASE_FRAMES := 1800

## Two lap times agree exactly when they agree to within half a tick.
## `session_probe.gd`'s constant and its argument: the sector sum is exact in
## integer ticks and only inexact in the double they are multiplied into.
const HALF_TICK_S := 0.5 / 120.0

## The grid a saved lap time lives on, in seconds.
##
## **A round trip cannot be exact in seconds and asserting that it is would be a
## flaky gate.** `src/core/profile.h:63` is explicit: *"a lap time and a tunable
## are quantized to the same 1e-6 grid and rendered by the same integer
## arithmetic"*, and `format_value` writes six decimals. A lap of 7,049 ticks at
## 120 Hz is 58.741666... s, which is not a multiple of a microsecond, so it comes
## back 333 ns different — measured. A lap of 7,206 ticks is exactly 60.050000 and
## comes back bit-identical, which is why a 1e-9 assertion would pass or fail on
## whether the driver happened to land on a microsecond.
##
## So the assertion is the one the format actually makes: the two agree **exactly**
## as integers on that grid, which is the same comparison `set_best` uses to decide
## whether one lap beats another. The raw delta is printed either way.
const TIME_GRID_HZ := 1.0e6


static func _micros(seconds: float) -> int:
	return int(round(seconds * TIME_GRID_HZ))

# --- state -------------------------------------------------------------------

var _args := {}
var _cases: PackedStringArray = []
var _break := ""
var _patches: PackedStringArray = []
var _patch_arg := ""
var _checks: Array = []
var _notes := PackedStringArray()
var _trace_every := 0

var _shell: Node
var _stack: ScreenStack
var _circuit: Node
var _runner: SessionRunner
var _kart: KartBody
var _ledger: Node
var _track: KartTrack

var _length := 0.0
var _top_speed_ms := 40.0
var _radius := PackedFloat64Array()
var _input := {"throttle": 0.0, "brake": 0.0, "steer": 0.0}
var _station := 0.0
var _lateral := 0.0
var _target_ms := 0.0

var _tick := 0
var _phase := 0
var _phase_frame := 0
var _frames := 0

## Everything the walk saw, so a check reads what happened rather than re-deriving
## it. `_laps` rows are `[number, time_s, valid, reason]`, from the runner's own
## signal — the only moment `last_sectors()` is the lap it names.
var _laps: Array = []
var _lap_sectors: Array = []

var _setup_config := {}
var _setup_hops := -1
var _go_text := ""

## The two pause episodes: `[wanted setting, reason before, reason after,
## suspended during, suspended after, the screen's own sentence]`.
var _episodes: Array = []
var _episode_index := 0
var _episode_stage := 0
var _episode_hold := 0
var _pause_transport := ""

var _quit_hops := -1

var _result_pinned := -1.0
var _result_rows := -1
var _result_masthead := ""
var _footer := ""

var _best_before := 0
var _best_after := 0
var _reloaded_s := -1.0
var _reloaded_forgiven := false
var _ghost_id := ""

## The real `user://settings.cfg`, byte for byte, taken before anything is written
## and put back in `_report()`.
##
## **This is here because `circuit.gd:_strike_paused_lap()` builds a bare
## `KartSettings` with no base dir**, so `--profile-dir` does not reach it and
## there is no way to point the pause setting anywhere else. The alternative was
## not measuring the thing this gate exists to measure. See the report.
var _settings_backup: PackedByteArray = PackedByteArray()
var _settings_existed := false
var _settings_original := true
var _settings_restored := false


func _initialize() -> void:
	_args = Cmdline.parse()
	_trace_every = Cmdline.as_int(_args, "trace", 0)

	if Cmdline.as_bool(_args, "list-breaks", false):
		# Prefixed and filtered by the wrapper, because Godot prints its version
		# banner on stdout before a script runs and a bare list is five engine
		# words plus the mode names.
		for mode: String in BREAK_MODES:
			print("break-mode %s" % mode)
		quit(0)
		return

	var wanted := Cmdline.as_string(_args, "case", "")
	_cases = CASES if wanted.is_empty() else wanted.split(",", false)

	_break = Cmdline.as_string(_args, "break", "")
	if not _break.is_empty() and not BREAK_MODES.has(_break):
		printerr("unknown --break=%s; modes are %s"
				% [_break, ", ".join(PackedStringArray(BREAK_MODES.keys()))])
		quit(2)
		return

	_patch_arg = Cmdline.as_string(_args, "patch", "auto")
	if _patch_arg != "auto" and _patch_arg != "none":
		for name: String in _patch_arg.split(",", false):
			if not PATCHES.has(name):
				printerr("unknown --patch=%s; patches are %s"
						% [name, ", ".join(PackedStringArray(PATCHES.keys()))])
				quit(2)
				return

	root.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_IGNORE
	root.content_scale_size = VIEWPORT

	if not ClassDB.class_exists("KartTrack") or not ClassDB.class_exists("KartProfile"):
		_fail("the extension is registered",
				"KartTrack/KartProfile are not in ClassDB -- "
				+ "scons target=editor arch=arm64")
		_report()
		return

	if not _prepare_track_copy():
		_report()
		return
	_back_up_settings()

	# What the profile held before the walk, so the write is a measured difference
	# and not an assertion that a file exists. `--break=nolap` turns on this pair.
	var before := KartProfile.new()
	before.set_base_dir(BASE_DIR)
	before.load()
	_best_before = before.best_count()

	_check_blockers()

	if not _driven():
		_report()
		return

	var packed := load(SHELL_SCENE) as PackedScene
	if packed == null:
		_fail("the shell scene loads", "%s did not load" % SHELL_SCENE)
		_report()
		return
	_shell = packed.instantiate()
	# Before `add_child`, because `_ready()` reads it. The command line carries the
	# same two keys — see `_shell_args()` — so the shell the *session* comes back
	# to, which nothing here instantiates, is redirected too.
	_shell.set("arg_override", _shell_args())
	root.add_child(_shell)
	# So `change_scene_to_file()` frees this one. A `--script` main loop has no
	# current scene of its own, and without this the shell stays in the tree behind
	# the circuit with a live `ScreenStack` eating the pause key.
	current_scene = _shell


## The two arguments the shell must come up with, wherever it is built.
##
## `profile-dir` is the load-bearing one: the results sheet files a best lap
## through `best_lap_store` as part of showing it, so a walk against the default
## `user://` would seat a probe's lap at the top of the runner's real career,
## written through the same fsync'd atomic save a real lap uses.
func _shell_args() -> Dictionary:
	return {"backdrop": "flat", "profile-dir": BASE_DIR}


# --- the track copy ---------------------------------------------------------


## Copy the circuit to `user://walk_probe/` under a slug nothing else uses.
##
## Byte for byte, so the content hash the session records is the shipped
## circuit's — `KartTrack::load` hashes the file's own bytes, and a reformat would
## make this a different track. Only the *name* changes, and the name is what the
## ghost id and the profile key are derived from.
func _prepare_track_copy() -> bool:
	var dir := ProjectSettings.globalize_path(BASE_DIR)
	DirAccess.make_dir_recursive_absolute(dir)
	if not FileAccess.file_exists(TRACK_SOURCE):
		_fail("the circuit file is there", "no %s" % TRACK_SOURCE)
		return false
	var bytes := FileAccess.get_file_as_bytes(TRACK_SOURCE)
	var out := FileAccess.open(TRACK_COPY, FileAccess.WRITE)
	if out == null:
		_fail("the circuit copy is written", "could not open %s" % TRACK_COPY)
		return false
	out.store_buffer(bytes)
	out.close()

	_track = KartTrack.new()
	if _track.load(TRACK_COPY) != OK or not _track.is_loaded():
		_fail("the circuit copy loads", "; ".join(_track.problems()))
		_track = null
		return false
	_track.select_layout("forward")
	_length = _track.length()
	_top_speed_ms = KartCore.kmh_to_ms(float(KartCore.kz_reference()["top_speed_max_kmh"]))
	_build_radius_table()
	return true


## The centerline radius every `RADIUS_STEP_M`, from the file's own curvature.
##
## `sample()["curvature"]` is authored rather than differenced out of headings —
## `distance_m` and `curvature_1pm` are the normative pair and `position` is a
## checksum — so a straight reads exactly zero and `INF` means "not a corner"
## rather than "a very large corner".
func _build_radius_table() -> void:
	var count := maxi(1, int(_length / RADIUS_STEP_M))
	_radius.resize(count)
	for index: int in count:
		var curvature: float = absf(float(
				_track.sample(float(index) * RADIUS_STEP_M)["curvature"]))
		_radius[index] = INF if curvature < 1e-9 else 1.0 / curvature


# --- the real settings file, borrowed and put back --------------------------


## Nothing is borrowed any more, and that is a fix rather than a simplification.
##
## This probe used to take a byte-for-byte copy of the **real** `user://settings.cfg`,
## write its own value into it and put it back at the end, because `circuit.gd`
## built its `KartSettings` with no base dir and `--profile-dir` therefore could not
## reach the one field the pause checks depend on. Every worktree shares one
## `user://`, so that was a probe reaching into Anthony's own settings file and
## relying on an orderly exit to put it back.
##
## `circuit.gd::_strike_paused_lap` now honors `--profile-dir`, so the setting is
## written where the session will look for it and the real file is never opened.
## The backup fields stay declared and empty: `_restore_settings` is still called on
## every exit path and a scrub with nothing to scrub is the cheapest way to keep
## that guarantee true.
func _back_up_settings() -> void:
	_settings_existed = false
	_settings_backup = PackedByteArray()
	_settings_original = false
	_notes.append("the real user://settings.cfg is never opened: circuit.gd honors "
			+ "--profile-dir, so the pause setting is written to " + BASE_DIR
			+ "settings.cfg and read back from there")


## Set the one field `circuit.gd` reads, through the class that owns the format.
func _write_pause_setting(enabled: bool) -> bool:
	var settings := KartSettings.new()
	# The same redirect `circuit.gd` applies when it reads this back. If the two ever
	# disagree the pause checks silently measure the default instead of the value
	# under test, which is exactly what happened when only one end honored it.
	settings.set_base_dir(BASE_DIR)
	settings.load()
	settings.set_pause_invalidates_lap(enabled)
	var saved: Dictionary = settings.save()
	return bool(saved.get("ok", false))


func _restore_settings() -> void:
	if _settings_restored:
		return
	_settings_restored = true
	var path := "user://settings.cfg"
	# Nothing was borrowed, so there is nothing to put back. The scrub of
	# BASE_DIR takes the settings file this probe actually wrote.
	if not _settings_existed:
		return
	var out := FileAccess.open(path, FileAccess.WRITE)
	if out == null:
		printerr("walk_probe: could not restore user://settings.cfg; "
				+ "pause_invalidates_lap was %s" % ["on" if _settings_original else "off"])
		return
	out.store_buffer(_settings_backup)
	out.close()


# --- the blockers -----------------------------------------------------------


func _check_blockers() -> void:
	_check_hint_projection()


## **`Track::project()` cannot carry a hint forward, and it stops the walk dead.**
##
## `src/core/track.h:437` builds the hinted search window like this:
##
##     first = span_at(wrap(hint - window));
##     covered = 0; for (...) { covered += span_length(...); if (covered >= 2*window) break; }
##
## `covered` is accumulated from the **start of the span containing `hint - 30`**,
## not from `hint - 30` itself, so the window is 60 m measured from wherever that
## control point happens to begin. On a long span it therefore reaches almost
## nowhere ahead of the hint. Measured on Valdirone Nuova, where control point 10
## starts at 222.431 m and spans 38.861 m:
##
##     hint 250.0 -> window [222.431, 283.222], 33.2 m of forward reach
##     hint 280.0 -> window [222.431, 283.222],  3.2 m
##     hint 283.0 -> window [222.431, 283.222],  0.2 m
##     hint 290.0 -> window [222.431, 283.222], -6.8 m — the hint is not even in it
##
## Past the window the point clamps to the last span's end, reports a gap equal to
## the overshoot, wins on gap because nothing else is closer, and the answer is
## the window's end. The hint is then that same value forever.
##
## `SessionRunner._tick_running()` projects with a carried hint every tick, so its
## arc length freezes, the odometer stops, `LapTimer::advance()` sees no progress
## and **no lap of the circuit can ever close.** This is measured here rather than
## inferred: the walk below drives it.
##
## Unhinted projection is exact — worst 0.0000 m over the lap at 5 m stations — so
## the fault is in the window and not in the geometry, which is why `--patch=hint`
## is a legal workaround at all.
##
## **Measured whether or not `--case=blockers` asked for it**, and only
## *reported* under that case. The patch decision comes out of this measurement,
## so a subset run that skipped it would drive the unpatched walk while printing
## a report about the patched one — which is the "a subset run quietly measures
## something else" defect `shell_probe.gd` already shipped once. It needs no scene
## and costs about 4,000 arithmetic projections.
func _check_hint_projection() -> void:
	var total := _length
	var hint := 0.0
	var worst := 0.0
	var worst_at := 0.0
	var stuck_at := -1.0
	var stuck_to := -1.0
	var station := 0.0
	# 0.33 m a step, which is 40 m/s at 120 Hz — the real per-tick case.
	while station < total:
		var point: Vector3 = _track.sample(station)["position"]
		var got: Dictionary = _track.project(point, hint)
		var distance := float(got["distance"])
		var error := absf(fposmod(distance - station + total * 0.5, total) - total * 0.5)
		if error > worst:
			worst = error
			worst_at = station
		if error > 1.0 and stuck_at < 0.0:
			stuck_at = station
			stuck_to = distance
		hint = distance
		station += 0.33

	var ok := worst < 0.01
	if _wants("blockers"):
		_record("the hinted projection carries forward", ok,
				("worst %.4f m over a walk of the whole lap" % worst) if ok
				else "arc length sticks at %.3f m: the kart reaches %.3f m and the hinted "
						% [stuck_to, stuck_at]
						+ "projection still answers %.3f m; worst %.1f m at %.1f m. "
						% [stuck_to, worst, worst_at]
						+ "src/core/track.h:437 measures the window from the span start, "
						+ "not from the hint")
	if not ok:
		_need_patch("hint")


## Whether a patch is wanted: asked for by name, or `auto` and its defect measured.
func _need_patch(name: String) -> void:
	if _patch_arg == "none":
		return
	if _patch_arg != "auto" and not _patch_arg.split(",", false).has(name):
		return
	if not _patches.has(name):
		_patches.append(name)


func _patched(name: String) -> bool:
	return _patches.has(name)


# --- the walk ---------------------------------------------------------------
#
# One phase per frame. `_process` runs after the tree's nodes have processed and
# is where the menus and the scene swaps live; `_physics_process` runs *before*
# them — measured, and `session_probe.gd` depends on the same ordering — which is
# the only reason the hint patch can get in front of the runner's own tick.

enum {
	PH_SHELL_UP,
	PH_BOOT,
	PH_PADDOCK,
	PH_SETUP,
	PH_GO,
	PH_LOADING,
	PH_ATTACH,
	PH_DRIVE,
	PH_LEAVE,
	PH_BACK_IN_SHELL,
	PH_AFTERMATH,
	PH_DONE,
}


func _process(_delta: float) -> bool:
	_frames += 1
	_phase_frame += 1
	# The drive is counted in ticks and has `MAX_TICKS`; every other phase is a
	# handful of frames. One watchdog per clock, or the frame one fires 12,000
	# ticks into a perfectly healthy lap.
	if _phase != PH_DRIVE and _phase_frame > MAX_PHASE_FRAMES:
		_fail("the walk finishes", "stuck in phase %d for %d frames"
				% [_phase, _phase_frame])
		_report()
		return true

	match _phase:
		PH_SHELL_UP: _phase_shell_up()
		PH_BOOT: _phase_boot()
		PH_PADDOCK: _phase_paddock()
		PH_SETUP: _phase_setup()
		PH_GO: _phase_go()
		PH_LOADING: _phase_loading()
		PH_ATTACH: _phase_attach()
		PH_DRIVE: pass          # `_physics_process` owns the drive
		PH_LEAVE: _phase_leave()
		PH_BACK_IN_SHELL: _phase_back_in_shell()
		PH_AFTERMATH: _phase_aftermath()
		_:
			_report()
			return true
	return false


func _advance(next: int) -> void:
	_phase = next
	_phase_frame = 0


# --- 1. the shell, pad only -------------------------------------------------


func _phase_shell_up() -> void:
	# Two frames: `ScreenStack` lands focus the frame after a push, on purpose.
	if _phase_frame < 3:
		return
	_stack = _shell.call("stack") as ScreenStack
	if _stack == null:
		_fail("the shell builds a stack", "ShellRoot.stack() is null")
		_advance(PH_DONE)
		return
	_record_in("shell", "the shell opens on boot", _top_title() == "boot" and _stack.depth() == 1,
			"depth %d, top %s, focus %s" % [_stack.depth(), _top_title(), _focus_text()])
	_advance(PH_BOOT)


func _phase_boot() -> void:
	if _phase_frame == 1:
		_press("menu_confirm")
		return
	if _phase_frame < 4:
		return
	_record_in("shell", "boot hands over to the paddock, pad only",
			_top_title() == "paddock",
			"top %s after one %s menu_confirm, focus %s"
					% [_top_title(), _pause_transport, _focus_text()])
	_advance(PH_PADDOCK)


func _phase_paddock() -> void:
	if _phase_frame == 1:
		# The paddock lands focus on its first selectable row and that row is
		# Practice — `paddock_screen.gd` builds it first. Asserted rather than
		# assumed, because confirming the wrong row opens the wrong screen and the
		# next check would report "setup is not built".
		_record_in("shell", "the paddock opens on Practice", _focus_text() == "Practice",
				"focus is %s" % _focus_text())
		_press("menu_confirm")
		return
	if _phase_frame < 4:
		return
	_record_in("shell", "Practice opens the setup sheet, pad only", _top_title() == "setup",
			"top %s, focus %s" % [_top_title(), _focus_text()])
	_advance(PH_SETUP)


## Walk the pad down the sheet to `Go to track`, counting presses.
##
## Navigated rather than reached for: `ScreenStack._navigate` is what a thumb
## drives, and the sheet's fixed rows are `FOCUS_NONE` on purpose, so the hop
## count is the measurement — a sheet that grew an unreachable Go button would
## show up here as a walk that never arrives rather than as a button nobody
## pressed.
func _phase_setup() -> void:
	var top := _stack.top()
	if top == null:
		_fail("the setup sheet is on the stack", "nothing on top")
		_advance(PH_DONE)
		return
	if _phase_frame == 1:
		_setup_config = top.call("config") if top.has_method("config") else {}
		var refusal: String = top.call("refusal") if top.has_method("refusal") else "?"
		_record_in("shell", "the setup sheet would start a session", refusal == "",
				"config %s, refusal %s" % [_setup_config, "(none)" if refusal == "" else refusal])
		return
	# One press a frame, so the focus the next frame reads is the one this press
	# moved. Eight presses is past the sheet's row count either way.
	if _focus_text() == "Go to track":
		_setup_hops = _phase_frame - 2
		_go_text = _focus_text()
		_record_in("shell", "the pad reaches Go to track", true,
				"%d menu_down press%s from the first value row, focus on '%s'"
						% [_setup_hops, "" if _setup_hops == 1 else "es", _go_text])
		_advance(PH_GO)
		return
	if _phase_frame <= 9:
		_press("menu_down")
		return
	_record_in("shell", "the pad reaches Go to track", false,
			"8 menu_down presses never landed on it; focus is %s" % _focus_text())
	_advance(PH_GO)


func _phase_go() -> void:
	if _phase_frame == 1:
		if _setup_hops < 0:
			_fail("Go to track starts a session", "the pad never reached the button")
			_advance(PH_DONE)
			return
		_press("menu_confirm")
		return
	_advance(PH_LOADING)


# --- 2. the hand-off --------------------------------------------------------


## Wait for `change_scene_to_file` to land. `start_session()` awaits two process
## frames before it swaps, on purpose — without them the loading screen is built
## and freed inside one frame and is never drawn — and the swap itself is
## deferred, so this is three separate frames of waiting and not one.
func _phase_loading() -> void:
	var scene := current_scene
	if scene == null or scene == _shell:
		return
	_circuit = scene
	_advance(PH_ATTACH)


## The kart, the runner and the ledger, found on the first frame the circuit is
## current — not in `_initialize`, which is the `--script` trap this project has
## already paid for twice.
func _phase_attach() -> void:
	if _phase_frame < 2:
		return
	_runner = _circuit.get_node_or_null("SessionRunner") as SessionRunner
	_kart = _circuit.find_child("Kart", true, false) as KartBody
	if _runner == null or _kart == null:
		_fail("the circuit scene came up with a session",
				"runner %s, kart %s" % [_runner, _kart])
		_advance(PH_LEAVE)
		return
	if _runner.state() == SessionRunner.STATE_REFUSED:
		_fail("the circuit scene came up with a session",
				"refused: %s" % _runner.refusal())
		_advance(PH_LEAVE)
		return

	_check_handoff()
	_check_ledger_attached()

	# **The Callable overrides the scene's `PlayerDriver`.** `KartBody::gather_input`
	# gives `input_driver` precedence over pushed input; the ordering the other way
	# round once made all four `drive.sh` scenarios agree on one hash while
	# measuring nothing.
	_kart.input_driver = _pace_driver
	_runner.lap_completed.connect(_on_lap_completed)
	_advance(PH_DRIVE)


## Check 2. The scene is running the session the sheet asked for.
##
## The `track` key is the one the command line overrides, and that is
## `session_request.gd`'s documented merge rather than a hole: *"the posted keys
## go in first and the command-line keys second, so the command line wins every
## key it carries"*. This probe carries `--track=` for the `user://` safety reason
## in the header, so what is asserted is that the sheet's own `layout` and
## `session` survived the merge and that the override went where it was aimed.
func _check_handoff() -> void:
	if not _wants("handoff"):
		return
	var session: KartSession = _runner.get("_session")
	var problems := PackedStringArray()
	if session == null:
		problems.append("the runner has no KartSession")
	else:
		if String(session.get_track()) != TRACK_SLUG:
			problems.append("track is %s, not the %s the command line named"
					% [session.get_track(), TRACK_SLUG])
		if session.get_layout() != KartSession.LAYOUT_FORWARD:
			problems.append("layout is %d and the sheet posted %s"
					% [session.get_layout(), _setup_config.get("layout", "?")])
		if String(session.type_name(session.get_type())).to_lower() \
				!= String(_setup_config.get("session", "practice")).to_lower():
			problems.append("type is %s and the sheet posted %s"
					% [session.type_name(session.get_type()),
					_setup_config.get("session", "?")])
		if session.get_kart_class() != KartSession.CLASS_KZ2:
			problems.append("class is %d" % session.get_kart_class())
	_record("the session is the one the sheet asked for", problems.is_empty(),
			("%s %s %s, hash %s; the sheet posted %s and --track= won the track key, "
					% [session.get_track(), KartSession.layout_name(session.get_layout()),
					session.type_name(session.get_type()), session.get_track_hash_hex(),
					_setup_config]
					+ "which is SessionRequest's documented merge")
			if problems.is_empty() and session != null else ", ".join(problems))


## Check 4a. **Nothing builds the lap ledger.**
##
## `circuit.gd:977` reads `get_node_or_null("LapLedger")` and `lap_ledger.gd`'s
## own header says *"`circuit.gd` parents this as a node named `LapLedger`"* — and
## `grep -rn LapLedger scripts scenes tools` returns that one read and nothing
## else in the whole project. So `_ledger_rows()` answers `[]` on every session,
## `SessionRequest.deliver()` carries no laps, and the results sheet's table is
## empty on every real session anybody drives. `results_screen.gd` even has a
## branch for it — *"No ledger — an older scene, or a session whose ledger was
## never attached"* — and that branch is the only one that has ever run.
##
## The same "built at both ends and not joined in the middle" shape as
## `assist_auto_shift`, `look_back` and the pause menu itself.
##
## Measured on every run and reported under `--case=ledger`, for the same reason
## the projection check is: the patch decision comes out of it, and a subset run
## that skipped the decision would drive a different walk than it says it did.
func _check_ledger_attached() -> void:
	var found := _circuit.get_node_or_null("LapLedger")
	if _wants("ledger"):
		_record("circuit.gd attaches a LapLedger", found != null,
				"circuit.gd:977 reads a child named LapLedger and nothing in scripts/, "
				+ "scenes/ or tools/ ever creates one" if found == null
				else "found %s" % found.get_class())
	if found != null:
		_ledger = found
		return
	_need_patch("ledger")
	if not _patched("ledger"):
		return
	# The missing line, added here so everything downstream of the ledger can be
	# measured. `configure()` takes a duck-typed course and the kart for the speed
	# trap, exactly as its own docstring specifies.
	var built := LapLedger.new()
	built.name = "LapLedger"
	_circuit.add_child(built)
	if built.configure(_runner, _track, _kart):
		_ledger = built
		_notes.append("patch ledger: a LapLedger was attached by the probe, trap at "
				+ "%.1f m" % float(built.trap_station_m()))
	else:
		_notes.append("patch ledger: LapLedger.configure() refused")


# --- 3. the drive -----------------------------------------------------------


func _physics_process(_delta: float) -> bool:
	if _phase != PH_DRIVE or _runner == null:
		return false
	_tick += 1
	if _tick > MAX_TICKS:
		_fail("the walk finishes", "%d ticks and only %d crossings"
				% [_tick, _laps.size()])
		_advance(PH_LEAVE)
		return false

	# **The hint patch, and it has to be here.** This runs before any node's
	# `_physics_process`, so clearing the runner's stale hint puts an unhinted —
	# and therefore exact — projection in front of its own tick. It costs the
	# runner's odometer, which measures a step against that same hint and rejects
	# a 280 m one, so nothing below asserts on `odometer_m()`.
	if _patched("hint"):
		_runner.set("_hint", -1.0)

	_drive()
	_run_episode()
	_trace()

	if _laps.size() >= _crossings_wanted() and _episode_stage == 0 \
			and _episode_index >= _episodes_wanted():
		_advance(PH_LEAVE)
	return false


## How many start-line crossings the walk drives before it leaves.
##
## `--break=nolap` cuts it to the out lap alone, which is the sabotage: a session
## that ends before any *timed* lap has `has_best` false, and the negative control
## is that `best_lap_store` then writes nothing at all. `lap_timing.h` marks the
## first crossing `OutLap` precisely so it cannot become a best, so this is the
## real "no lap was set" case and not a synthetic one.
func _crossings_wanted() -> int:
	return 1 if _break == "nolap" else CROSSINGS_WANTED


## What the kart is handed this tick. Held in a member and returned by a
## Callable, because `KartBody::gather_input` calls it during the *kart's* physics
## step, which is later than this.
func _pace_driver() -> Dictionary:
	return _input


func _drive() -> void:
	if not _runner.is_running():
		_input = {"throttle": 0.0, "brake": 0.0, "steer": 0.0}
		return
	# **Unhinted, deliberately, and it is not the same call the runner makes.**
	# The hinted window is broken (see `_check_hint_projection`), and a controller
	# steering off a frozen arc length drives into the scenery — measured, at
	# 283.2 m, every time. The unhinted search is exact on this circuit.
	var placed := _track.project(_kart.global_position, -1.0)
	_station = placed["distance"]
	_lateral = placed["lateral"]

	# The runner suspends the driver while the pause menu is up; the probe's
	# Callable would otherwise drive straight through it, because `input_driver`
	# outranks the gate `PlayerDriver.enabled` implements.
	if _runner.is_input_suspended():
		_input = {"throttle": 0.0, "brake": 0.0, "steer": 0.0}
		return

	var speed: float = _kart.speed_ms
	var look := clampf(speed * LOOK_SECONDS, LOOK_MIN_M, LOOK_MAX_M)
	var aim: Dictionary = _track.sample(fposmod(_station + look, _length))
	var to_aim: Vector3 = (aim["position"] as Vector3) - _kart.global_position
	to_aim.y = 0.0
	var forward := -_kart.global_transform.basis.z
	forward.y = 0.0

	# The sign is `steering.h`'s convention — positive lock turns left — and
	# `signed_angle_to` is positive counter-clockwise about UP with Godot's
	# forward at -Z, so a target to the right is already negative. **No negation**;
	# `session_probe.gd` has the paragraph on what negating it looks like.
	var heading_error := 0.0
	if to_aim.length_squared() > 1e-6 and forward.length_squared() > 1e-6:
		heading_error = forward.signed_angle_to(to_aim, Vector3.UP)
	var steer := heading_error * STEER_GAIN_HEADING + _lateral * STEER_GAIN_LATERAL

	_target_ms = _corner_speed_ms(_tightest_radius_ahead(_station))
	var throttle := 0.0
	var brake := 0.0
	if speed < _target_ms - SPEED_BAND_MS:
		throttle = 1.0
	elif speed > _target_ms + SPEED_BAND_MS:
		brake = clampf((speed - _target_ms) / 8.0, 0.15, 1.0)
	else:
		throttle = 0.35
	_input = {"throttle": throttle, "brake": brake, "steer": clampf(steer, -1.0, 1.0)}


## Speed the kart holds at a given radius, m/s, interpolated in **log radius**
## because the six measured rows span a factor of twenty-two.
func _corner_speed_ms(radius: float) -> float:
	var last := CORNER_RADIUS_M.size() - 1
	if radius >= CORNER_RADIUS_M[last]:
		return _top_speed_ms
	if radius <= CORNER_RADIUS_M[0]:
		return KartCore.kmh_to_ms(CORNER_SPEED_KMH[0])
	for index: int in range(1, CORNER_RADIUS_M.size()):
		if radius > CORNER_RADIUS_M[index]:
			continue
		var low := log(CORNER_RADIUS_M[index - 1])
		var high := log(CORNER_RADIUS_M[index])
		var travel := (log(radius) - low) / (high - low)
		return KartCore.kmh_to_ms(CORNER_SPEED_KMH[index - 1]
				+ travel * (CORNER_SPEED_KMH[index] - CORNER_SPEED_KMH[index - 1]))
	return _top_speed_ms


func _tightest_radius_ahead(from_m: float) -> float:
	var count := _radius.size()
	var index := int(fposmod(from_m, _length) / RADIUS_STEP_M) % count
	var tightest := INF
	var covered := 0.0
	while covered < CORNER_LOOKAHEAD_M:
		tightest = minf(tightest, _radius[index])
		index = (index + 1) % count
		covered += RADIUS_STEP_M
	return tightest


func _on_lap_completed(number: int, time_s: float, valid: bool, reason: String) -> void:
	_laps.append([number, time_s, valid, reason])
	# `last_sectors()` is this lap's **inside the handler** and the next lap's one
	# crossing later. Duplicated out, because the array is the timer's.
	_lap_sectors.append(_runner.timer().last_sectors().duplicate())


func _trace() -> void:
	if _trace_every <= 0 or _tick % _trace_every != 0:
		return
	print("t%6d  %-6s  d %7.1f lat %+6.2f  %5.1f km/h -> %5.1f  g%d  %-10s  ep%d.%d" % [
			_tick, _runner.state_name(), _station, _lateral,
			KartCore.ms_to_kmh(_kart.speed_ms), KartCore.ms_to_kmh(_target_ms),
			_kart.get_gear(), _runner.timer().current_reason(),
			_episode_index, _episode_stage])


# --- 4. the pause episodes --------------------------------------------------
#
# Two pauses in two genuine timed laps. The first has `pause_invalidates_lap`
# **off** and the lap has to survive; the second has it **on** and the lap has to
# be struck out as `paused`. In between, the setting is put back the way it was
# found.


## Which lap each episode belongs to. Lap 1 is the out lap — the grid is 4 m
## behind the line, so it closes in 1.7 s and `lap_timing.h` marks it `OutLap` —
## and a strike on an out lap is correctly a no-op, which is the whole reason M5f
## could not measure any of this from a parked kart.
func _episode_lap(index: int) -> int:
	return 2 + index


func _episodes_wanted() -> int:
	if _break == "nolap":
		return 0
	return 2 if _wants("strike") else 0


func _run_episode() -> void:
	if _episodes.size() >= _episodes_wanted():
		return
	if _episode_index >= _episodes_wanted():
		return

	var timer := _runner.timer()
	var lap_now := _runner.timer().laps_completed() + 1
	var fraction := fposmod(_station, _length) / _length

	match _episode_stage:
		0:
			if lap_now != _episode_lap(_episode_index):
				return
			if fraction < PAUSE_AT_FRACTION or fraction > PAUSE_AT_FRACTION + 0.08:
				return
			if String(timer.current_reason()) != "valid":
				return
			var strict := _episode_index == 1
			# `--break=strike` writes the permissive setting into the strict
			# episode, so the strike does not happen and the check aimed at it must
			# go red. It is the one sabotage that has to reach the disk, because
			# that is where `circuit.gd` reads it from.
			if _break == "strike" and strict:
				strict = false
			if not _write_pause_setting(strict):
				_notes.append("episode %d: settings.cfg would not save" % _episode_index)
			_episode_record = {
				"lap": lap_now,
				"strict": _episode_index == 1,
				"at_m": _station,
				"before": String(timer.current_reason()),
				"suspended_before": _runner.is_input_suspended(),
			}
			_pause_open()
			_episode_hold = 0
			_episode_stage = 1
		1:
			_episode_hold += 1
			if _episode_hold < PAUSE_HOLD_FRAMES:
				return
			_episode_record["after"] = String(timer.current_reason())
			_episode_record["suspended"] = _runner.is_input_suspended()
			_episode_record["depth"] = _pause_depth()
			_episode_record["sentence"] = _pause_sentence()
			_pause_close()
			_episode_hold = 0
			_episode_stage = 2
		2:
			_episode_hold += 1
			if _episode_hold < 3:
				return
			_episode_record["suspended_after"] = _runner.is_input_suspended()
			_episode_record["depth_after"] = _pause_depth()
			_episodes.append(_episode_record)
			_episode_index += 1
			_episode_stage = 0
			if _episode_index >= _episodes_wanted():
				# Put the driver's own preference back the moment the last episode is
				# done rather than at the end of the run, so the window in which a
				# crash could leave it moved is as short as it can be.
				_restore_settings()


var _episode_record := {}


## Open the pause menu the way a driver does: the `pause` action, through
## `circuit.gd`'s `_unhandled_input`.
##
## Escape and Circle carry both `pause` and `menu_back`. With the pause stack
## empty `ScreenStack._input()` returns without consuming, so the press falls
## through to the scene; with the menu up the stack eats it and pops itself. One
## key, both directions, and driving it as one key is what proves that.
func _pause_open() -> void:
	_press("pause")


func _pause_close() -> void:
	_press("menu_back")


func _pause_stack() -> ScreenStack:
	return _circuit.get("_pause_stack") as ScreenStack


func _pause_depth() -> int:
	var stack := _pause_stack()
	return stack.depth() if stack != null else -1


## Which of the two authored sentences the pause screen is showing. It reads the
## same setting `circuit.gd` does, and `pause_screen.gd` is explicit that if the
## two disagree the screen is wrong — so this is a cheap way to catch them
## disagreeing.
func _pause_sentence() -> String:
	var stack := _pause_stack()
	if stack == null:
		return ""
	var top := stack.top()
	if top == null or not top.has_method("consequence_text"):
		return ""
	return String(top.call("consequence_text"))


# --- 5. out through the pause menu ------------------------------------------


## Leave the way a driver leaves: pause, walk down to `Quit to paddock`, confirm.
##
## `GAMEDESIGN.md` §4 gives Practice no limit, so `end_session()` is its only
## ending and this menu is the only thing that calls it. A results sheet is
## unreachable by any other route.
func _phase_leave() -> void:
	if _runner == null or _circuit == null:
		_advance(PH_BACK_IN_SHELL)
		return
	if _phase_frame == 1:
		_check_laps()
		_check_ledger_rows()
		_check_strike()
		_pause_open()
		return
	if _phase_frame == 2:
		return
	var text := _focus_text()
	if text == "Quit to paddock":
		_quit_hops = _phase_frame - 3
		_press("menu_confirm")
		_record_in("shell", "the pad reaches Quit to paddock and leaves", true,
				"pause depth %d, %d menu_down press%s from Resume, then menu_confirm"
						% [_pause_depth(), _quit_hops,
						"" if _quit_hops == 1 else "es"])
		_advance(PH_BACK_IN_SHELL)
		return
	if _phase_frame <= 10:
		_press("menu_down")
		return
	_record_in("shell", "the pad reaches Quit to paddock and leaves", false,
			"8 presses never landed on it; focus is %s, pause depth %d"
					% [_focus_text(), _pause_depth()])
	# Left anyway, so everything downstream is still measured rather than silently
	# skipped alongside the one thing that actually broke.
	_circuit.call("leave_session", "walk probe: the pad could not reach Quit")
	_advance(PH_BACK_IN_SHELL)


# --- 6, 7, 8. the results sheet, the profile and the ghost ------------------


var _swap_frames := 0


## Wait for the shell to come back up.
##
## **`ShellRoot.return_to_shell()` builds a fresh shell with no `arg_override` at
## all**, so the redirect that keeps this walk out of the runner's real career has
## to arrive on the command line — which is why `walk.sh` passes `--profile-dir=`
## and `--backdrop=flat` as user arguments as well as setting `arg_override` on
## the first shell. Miss that and the results sheet files the probe's lap into
## `user://profile.save` through the same fsync'd atomic write a real lap uses.
func _phase_back_in_shell() -> void:
	var scene := current_scene
	if scene == null or scene == _circuit:
		return
	_shell = scene
	# Counted from the swap and not from the phase, because the swap is deferred
	# and lands on whichever frame it lands on.
	_swap_frames += 1
	if _swap_frames < 3:
		return
	_stack = _shell.call("stack") as ScreenStack
	_advance(PH_AFTERMATH)


func _phase_aftermath() -> void:
	_check_results()
	_check_profile()
	_check_ghost()
	_advance(PH_DONE)


## Check 3. A complete timed lap closed.
func _check_laps() -> void:
	if not _wants("lap"):
		return
	var timer := _runner.timer()
	var lines := PackedStringArray()
	for row: Array in _laps:
		lines.append("lap %d %s %s%s" % [row[0], SessionRunner.format_time(row[1]),
				"valid" if row[2] else "STRUCK", "" if row[2] else " (" + row[3] + ")"])
	# **Valid, not merely closed.** The unpatched walk closes five laps and every
	# one of them is struck out as `off_track` by a projection that stopped
	# tracking the kart, so a check that counted crossings would report the circuit
	# working while nothing downstream of it could ever run. A lap that does not
	# count is not a lap this walk is about.
	var timed := 0
	for row: Array in _laps:
		if int(row[0]) > 1 and float(row[1]) > 0.0 and bool(row[2]):
			timed += 1
	_record("a complete lap of the circuit was driven", timed >= 1,
			"%d crossing%s in %d ticks (%s), %d of them a counting lap: %s. %s"
			% [_laps.size(), "" if _laps.size() == 1 else "s", _tick,
			SessionRunner.format_time(_runner.elapsed_s()), timed,
			", ".join(lines) if not lines.is_empty() else "none",
			"best %s over %d marks in %d sectors"
					% [SessionRunner.format_time(timer.best_time()),
					timer.mark_count(), timer.sector_count()]])


## Check 4b. The ledger has a row for the lap and the row's sectors sum to it.
##
## Read off the ledger rather than off the timer, because the ledger's row is what
## reaches the results sheet — `KartLapTimer` publishes `last_*` and `best_*` and
## no history at all, so a sheet that agreed with the timer and disagreed with the
## ledger would still print the wrong table.
func _check_ledger_rows() -> void:
	if not _wants("ledger"):
		return
	if _ledger == null:
		_record("the ledger row's sectors sum to the lap", false,
				"there is no ledger; see the check above")
		return
	var rows: Array = _ledger.call("rows")
	if _break == "sectors" and rows.size() > 0:
		# The sabotage has to reach the ledger's own stored row, not this copy, or
		# the results sheet downstream would still see the truth.
		var stored: Array = _ledger.get("_rows")
		for row: Dictionary in stored:
			var sectors: PackedFloat64Array = row["sectors"]
			if sectors.size() > 0:
				sectors[0] = sectors[0] * 1.5
				row["sectors"] = sectors
		rows = _ledger.call("rows")

	var checked := 0
	var worst := 0.0
	var worst_lap := 0
	var detail := PackedStringArray()
	for row: Dictionary in rows:
		var sectors: PackedFloat64Array = row["sectors"]
		if sectors.size() == 0:
			continue
		var sum := 0.0
		var parts := PackedStringArray()
		for split: float in sectors:
			sum += split
			parts.append("%.4f" % split)
		var error := absf(sum - float(row["time_s"]))
		checked += 1
		if error > worst:
			worst = error
			worst_lap = int(row["lap"])
		detail.append("lap %d %s = %s |d| %.9f" % [row["lap"],
				" + ".join(parts), SessionRunner.format_time(sum), error])
	# **And the rows are the laps they name.** `lap_ledger.gd`'s header is entirely
	# about this hazard: `last_sectors()` is the closing lap's only inside the
	# handler and silently becomes the next lap's at the following crossing, so a
	# sheet whose sector columns are off by one lap looks completely plausible and
	# is wrong on every row. This probe subscribed to the same signal
	# independently, so the two readings are two subscribers and not one.
	var mismatched := PackedStringArray()
	for index: int in mini(rows.size(), _laps.size()):
		var row: Dictionary = rows[index]
		var seen: Array = _laps[index]
		if int(row["lap"]) != int(seen[0]) or _micros(float(row["time_s"])) != _micros(float(seen[1])):
			mismatched.append("row %d says lap %d at %s, the runner signalled lap %d at %s"
					% [index, row["lap"], SessionRunner.format_time(float(row["time_s"])),
					seen[0], SessionRunner.format_time(float(seen[1]))])
			continue
		var mine: PackedFloat64Array = _lap_sectors[index]
		var theirs: PackedFloat64Array = row["sectors"]
		if mine.size() != theirs.size():
			mismatched.append("row %d has %d splits, the timer had %d"
					% [index, theirs.size(), mine.size()])
			continue
		for split: int in mine.size():
			if _micros(mine[split]) != _micros(theirs[split]):
				mismatched.append("row %d S%d is %.4f, the timer had %.4f"
						% [index, split + 1, theirs[split], mine[split]])

	_record("the ledger rows are the laps they name",
			rows.size() == _laps.size() and mismatched.is_empty(),
			"%d ledger row%s against %d crossings%s" % [rows.size(),
			"" if rows.size() == 1 else "s", _laps.size(),
			"; all agree lap for lap and split for split" if mismatched.is_empty()
					else ": " + ", ".join(mismatched)])

	var ok := checked >= 1 and worst <= HALF_TICK_S
	_record("the ledger row's sectors sum to the lap", ok,
			"%d row%s, %d with splits; worst |sum - lap| %.9f s%s. %s"
			% [rows.size(), "" if rows.size() == 1 else "s", checked, worst,
			"" if ok or checked < 1
					else ", the disagreement is +%.4f s on lap %d" % [worst, worst_lap],
			"; ".join(detail)])


## Check 5. The pause strike, both ways round.
##
## Four assertions and each is a different thing that has never been measured:
## that `pause` reaches `circuit.gd` at all, that the input gate closes and
## reopens, that with the setting **on** the reason moves `valid -> paused` and the
## lap is struck at the line, and that with it **off** the lap survives.
func _check_strike() -> void:
	if not _wants("strike"):
		return
	if _episodes.size() < 2:
		_record("pausing strikes the lap when the setting is on", false,
				"only %d of 2 pause episodes ran; the walk got %d crossings"
						% [_episodes.size(), _laps.size()])
		_record("pausing forgives the lap when the setting is off", false,
				"only %d of 2 pause episodes ran" % _episodes.size())
		return

	var forgiving: Dictionary = _episodes[0]
	var strict: Dictionary = _episodes[1]

	_record("the pause key opens the menu and closes it",
			int(forgiving.get("depth", -1)) == 1 and int(forgiving.get("depth_after", -1)) == 0
			and int(strict.get("depth", -1)) == 1,
			"depth %d while paused, %d after, through the real %s binding"
					% [int(forgiving.get("depth", -1)),
					int(forgiving.get("depth_after", -1)), _pause_transport])
	_record("pausing gates the input at the driver",
			not bool(forgiving.get("suspended_before", true))
			and bool(forgiving.get("suspended", false))
			and not bool(forgiving.get("suspended_after", true)),
			"suspended %s -> %s -> %s" % [forgiving.get("suspended_before"),
					forgiving.get("suspended"), forgiving.get("suspended_after")])

	var struck_lap := _lap_row(int(strict["lap"]))
	var strict_ok := String(strict.get("before", "")) == "valid" \
			and String(strict.get("after", "")) == "paused" \
			and struck_lap.size() == 4 and not bool(struck_lap[2]) \
			and String(struck_lap[3]) == "paused"
	_record("pausing strikes the lap when the setting is on", strict_ok,
			"lap %d at %.0f m: reason %s -> %s, and the lap closed %s. Screen said: %s"
			% [strict["lap"], float(strict["at_m"]), strict.get("before"),
			strict.get("after"),
			"STRUCK as '%s'" % struck_lap[3] if struck_lap.size() == 4 and not bool(struck_lap[2])
					else "valid",
			_short(String(strict.get("sentence", "")))])

	var kept_lap := _lap_row(int(forgiving["lap"]))
	var forgiving_ok := String(forgiving.get("before", "")) == "valid" \
			and String(forgiving.get("after", "")) == "valid" \
			and kept_lap.size() == 4 and bool(kept_lap[2])
	_record("pausing forgives the lap when the setting is off", forgiving_ok,
			"lap %d at %.0f m: reason %s -> %s, and the lap closed %s at %s. Screen said: %s"
			% [forgiving["lap"], float(forgiving["at_m"]), forgiving.get("before"),
			forgiving.get("after"),
			"valid" if kept_lap.size() == 4 and bool(kept_lap[2]) else "STRUCK",
			SessionRunner.format_time(float(kept_lap[1])) if kept_lap.size() == 4 else "-",
			_short(String(forgiving.get("sentence", "")))])


func _lap_row(number: int) -> Array:
	for row: Array in _laps:
		if int(row[0]) == number:
			return row
	return []


## Check 6. The sheet the driver lands on pins the lap that was driven.
func _check_results() -> void:
	if not _wants("results"):
		return
	if _stack == null:
		_record("the results sheet pins the lap that was driven", false,
				"the shell did not come back up")
		return
	var top := _stack.top()
	var title := top.title() if top != null else "<none>"
	if top == null or title != "results":
		_record("the results sheet pins the lap that was driven", false,
				"the shell opened %s and not the results sheet" % title)
		return
	_result_pinned = float(top.call("pinned_best_s")) if top.has_method("pinned_best_s") else -1.0
	_result_rows = int(top.call("row_count")) if top.has_method("row_count") else -1
	_footer = String(top.get("_footer_line"))
	var carried: Variant = top.get("_result")
	_result_masthead = String((carried as Dictionary).get("track_name", "")) \
			if carried is Dictionary else ""

	var driven := _best_valid_lap_s()
	_record("the results sheet pins the lap that was driven",
			driven > 0.0 and absf(_result_pinned - driven) < 1e-9,
			"sheet pins %s over %d row%s; the fastest valid lap driven was %s (delta %.9f s). "
			% [SessionRunner.format_time(_result_pinned), _result_rows,
			"" if _result_rows == 1 else "s", SessionRunner.format_time(driven),
			absf(_result_pinned - driven)]
			+ "Masthead '%s', footer '%s'" % [_result_masthead, _short(_footer)])


func _best_valid_lap_s() -> float:
	var best := -1.0
	for row: Array in _laps:
		if int(row[0]) <= 1 or not bool(row[2]) or float(row[1]) <= 0.0:
			continue
		if best < 0.0 or float(row[1]) < best:
			best = float(row[1])
	return best


## Check 7. The profile on disk holds it, and a *fresh* one reads it back.
##
## Fresh rather than the shell's own: the shell's `KartProfile` has the record in
## memory whether or not `save()` did anything, so reading it back through the
## same object would be a check that cannot fail. This one is constructed after
## the write, points at the same base dir, and calls `load()`.
func _check_profile() -> void:
	if not _wants("profile"):
		return
	var driven := _best_valid_lap_s()
	var fresh := KartProfile.new()
	fresh.set_base_dir(BASE_DIR)
	var loaded: Dictionary = fresh.load()
	_best_after = fresh.best_count()
	_reloaded_s = fresh.best_time(TRACK_SLUG, KartProfile.LAYOUT_FORWARD,
			KartProfile.CLASS_KZ2) if fresh.has_best(TRACK_SLUG,
			KartProfile.LAYOUT_FORWARD, KartProfile.CLASS_KZ2) else -1.0
	_reloaded_forgiven = fresh.best_pause_forgiven(TRACK_SLUG,
			KartProfile.LAYOUT_FORWARD, KartProfile.CLASS_KZ2)
	var ok := driven > 0.0 and _reloaded_s > 0.0 \
			and _micros(_reloaded_s) == _micros(driven)
	_record("the profile on disk holds the lap", ok,
			"best_count %d -> %d; %s reads %s for %s/forward/kz2 against %s driven, "
			% [_best_before, _best_after, fresh.profile_path(),
			SessionRunner.format_time(_reloaded_s), TRACK_SLUG,
			SessionRunner.format_time(driven)]
			+ "%d us against %d us on profile.h's 1e-6 grid, raw delta %.12f s "
			% [_micros(_reloaded_s), _micros(driven),
			absf(_reloaded_s - driven) if _reloaded_s > 0.0 else -1.0]
			+ "(load %s, %d bytes)"
			% [loaded.get("status_name", "?"), int(loaded.get("bytes", 0))])

	# **The pause flag the format carries and nothing sets.** `profile.h` added the
	# optional sixth token for #186 — *"a best set with that forgiveness enabled is
	# not the same achievement as one set without it"* — and `set_best`'s sixth
	# argument defaults false. `best_lap_store.gd:148` calls it with five, and
	# nothing anywhere carries "this lap was set while paused" out of the session:
	# `circuit.gd:_on_session_finished()` does not put it in the result, so the
	# store has nothing to pass even if it wanted to. The lap this walk filed *was*
	# set with forgiveness on, and the record says it was not.
	_record("the saved best records that pausing was forgiven", _reloaded_forgiven,
			"the lap was set with pause_invalidates_lap off and the record reads "
			+ "pause_forgiven %s. Nothing carries the flag: circuit.gd's result has "
					% _reloaded_forgiven
			+ "no such key and best_lap_store.gd:148 calls set_best with five "
			+ "arguments, so the sixth always defaults false")


## Check 8. The ghost this session recorded is on disk and loads.
##
## The id is minted the way `KartGhost` mints it rather than read out of the
## scene, so this is the id anything else looking for the ghost would compute.
func _check_ghost() -> void:
	if not _wants("ghost"):
		return
	_ghost_id = String(KartGhost.mint_id(TRACK_SLUG, KartProfile.LAYOUT_FORWARD,
			KartProfile.CLASS_KZ2))
	var path := String(KartGhost.path_for_id(_ghost_id)) if not _ghost_id.is_empty() else ""
	if _break == "ghost" and not path.is_empty() and FileAccess.file_exists(path):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(path))

	if path.is_empty() or not FileAccess.file_exists(path):
		# The profile pointer is what makes this a finding rather than a shrug: a
		# session that simply never recorded a ghost leaves no id in the `best`
		# record either, so a record that names one and a disk that has not got it
		# is a dangling reference and nothing else.
		_record("the session's ghost is on disk and loads", false,
				"%s is missing (id %s) and the profile points at %s"
						% [path if not path.is_empty() else "<no id>", _ghost_id,
						_profile_ghost_id()])
		return
	var ghost := KartGhost.new()
	var loaded: Dictionary = ghost.load_id(_ghost_id)
	if not bool(loaded.get("ok", false)):
		_record("the session's ghost is on disk and loads", false,
				"load_id(%s) refused: %s" % [_ghost_id, loaded])
		return
	var driven := _best_valid_lap_s()
	var lap := float(ghost.lap_time())
	_record("the session's ghost is on disk and loads",
			driven > 0.0 and absf(lap - driven) < HALF_TICK_S,
			"%s: %s over %d samples against %s driven, delta %.9f s; the profile "
			% [_ghost_id, SessionRunner.format_time(lap), ghost.sample_count(),
			SessionRunner.format_time(driven), absf(lap - driven)]
			+ "points at %s" % _profile_ghost_id())


func _profile_ghost_id() -> String:
	var fresh := KartProfile.new()
	fresh.set_base_dir(BASE_DIR)
	fresh.load()
	var id := String(fresh.best_ghost_id(TRACK_SLUG, KartProfile.LAYOUT_FORWARD,
			KartProfile.CLASS_KZ2))
	return id if not id.is_empty() else "<nothing>"


# --- input ------------------------------------------------------------------


## Press and release one action, **through a pad event where the action has one**.
##
## `shell_probe.gd` drives its walk with the keyboard event because it is
## measuring the `InputMap` binding path; this one is measuring that a person
## holding a DualSense can get from the boot screen to a filed lap time, so it
## sends the joypad event and only falls back to the key when an action has no pad
## binding at all. Which transport was used is reported, because a silent fallback
## would make "pad only" a claim rather than a measurement.
##
## The two `Input` calls are not belt and braces. `use_accumulated_input` defaults
## true, so a parsed event sits in the queue until the next frame's flush and every
## assertion made after a send reads the press before it.
func _press(action: StringName) -> void:
	_send(action, true)
	_send(action, false)
	Input.action_release(action)


func _send(action: StringName, pressed: bool) -> void:
	var pad: InputEvent = null
	var key: InputEvent = null
	for event: InputEvent in InputMap.action_get_events(action):
		if pad == null and event is InputEventJoypadButton:
			pad = event
		elif key == null and event is InputEventKey:
			key = event
	var chosen := pad if pad != null else key
	if chosen == null:
		_notes.append("action %s has no pad and no key binding" % action)
		return
	_pause_transport = "pad" if chosen == pad else "key"
	var copy := chosen.duplicate() as InputEvent
	copy.set("pressed", pressed)
	if copy is InputEventKey:
		(copy as InputEventKey).echo = false
	Input.use_accumulated_input = false
	Input.parse_input_event(copy)
	Input.flush_buffered_events()


# --- reporting --------------------------------------------------------------


func _wants(case_name: String) -> bool:
	return _cases.has(case_name)


func _driven() -> bool:
	for name: String in DRIVEN_CASES:
		if _cases.has(name):
			return true
	return false


func _record(name: String, ok: bool, measurement: String) -> void:
	_checks.append([name, ok, measurement])


## Record only when the case asked for it.
##
## The menu walk still has to *happen* under every case — a session cannot be
## started without pressing Go — so the presses are unconditional and only the
## assertions are gated. Doing it the other way round is how `--case=theme` in
## `shell_probe.gd` came to report `4 of 10` over six checks nobody asked about.
func _record_in(case_name: String, name: String, ok: bool, measurement: String) -> void:
	if _wants(case_name):
		_record(name, ok, measurement)


func _fail(name: String, measurement: String) -> void:
	_record(name, false, measurement)


func _top_title() -> String:
	var top := _stack.top() if _stack != null else null
	return top.title() if top != null else "<none>"


## What holds focus, as the text a person would read off it.
func _focus_text() -> String:
	var focused := root.gui_get_focus_owner()
	if focused == null:
		return "<nothing>"
	if focused is Button and not String((focused as Button).text).is_empty():
		return String((focused as Button).text)
	# A setup-sheet row is a Button with empty text carrying its labels as
	# children, so the name comes off the first Label under it.
	for node: Node in focused.find_children("*", "Label", true, false):
		var text := String((node as Label).text)
		if not text.is_empty():
			return text
	return String(focused.name)


static func _short(text: String) -> String:
	return text if text.length() <= 72 else text.substr(0, 69) + "..."


func _report() -> void:
	_restore_settings()

	var failures := 0
	for check: Array in _checks:
		var ok: bool = check[1]
		failures += 0 if ok else 1
		print("check %-52s %s   %s" % [check[0], "PASS" if ok else "FAIL", check[2]])
	for patch: String in _patches:
		print("patch %-52s APPLIED because \"%s\" is red" % [patch, PATCHES[patch]])
	if _patch_arg == "none":
		print("patch %-52s none, by request -- this is the unpatched walk" % "-")
	for note: String in _notes:
		print("note  %s" % note)
	# The case list, always. A subset run that prints "9 of 9 passed" reads exactly
	# like a full one and somebody will paste it into a ticket.
	print("cases: %s" % ("all %d" % CASES.size() if _cases == CASES
			else "%d of %d -- %s" % [_cases.size(), CASES.size(), ", ".join(_cases)]))
	print("walk probe: %d of %d checks passed%s in %d ticks and %d frames" % [
			_checks.size() - failures, _checks.size(),
			"" if failures == 0 else ", %d FAILED" % failures, _tick, _frames])

	if not Cmdline.as_bool(_args, "keep", false):
		_scrub()

	if _break.is_empty():
		quit(0 if failures == 0 else 1)
		return

	# **Inverted, and see the header.** The sabotage was aimed at one check by
	# name; that check going red is the negative control passing. Red is necessary
	# and, where the sabotage signs its work, not sufficient — this gate has three
	# checks that are red in a clean tree, so a mode that accepted any red would
	# report "caught" having done nothing.
	var target: String = BREAK_MODES[_break][0]
	var evidence: String = BREAK_MODES[_break][1]
	var ran := 0
	var red := 0
	var caught := PackedStringArray()
	for check: Array in _checks:
		if not String(check[0]).begins_with(target):
			continue
		ran += 1
		if bool(check[1]):
			continue
		red += 1
		if evidence.is_empty() or String(check[2]).contains(evidence):
			caught.append(String(check[0]))
	if ran == 0:
		print("negative control --break=%s: NOT CAUGHT -- no check named \"%s...\" ran, "
				% [_break, target] + "so the sabotage was aimed at nothing")
		quit(1)
		return
	print("negative control --break=%s: %s -- %d of %d \"%s...\" check%s went red%s, "
			% [_break, "caught" if not caught.is_empty() else "NOT CAUGHT",
			red, ran, target, "" if ran == 1 else "s",
			"" if evidence.is_empty() else ", %d naming \"%s\"" % [caught.size(), evidence]]
			+ "%d of %d red overall" % [failures, _checks.size()])
	quit(0 if not caught.is_empty() else 1)


## Everything this probe wrote. `profile_probe.gd`'s rule, and the ghost is the
## part that needs saying: it is the one file written outside `BASE_DIR`, because
## `KartGhost` has no `set_base_dir()` at all.
func _scrub() -> void:
	for id: String in KartGhost.stored_ids():
		if not id.begins_with(TRACK_SLUG.substr(0, 15)):
			continue
		var path := String(KartGhost.path_for_id(id))
		if not path.is_empty() and FileAccess.file_exists(path):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	_remove_tree(ProjectSettings.globalize_path(BASE_DIR))


static func _remove_tree(absolute: String) -> void:
	if not DirAccess.dir_exists_absolute(absolute):
		return
	for entry: String in DirAccess.get_directories_at(absolute):
		_remove_tree(absolute.path_join(entry))
	for entry: String in DirAccess.get_files_at(absolute):
		DirAccess.remove_absolute(absolute.path_join(entry))
	DirAccess.remove_absolute(absolute)
