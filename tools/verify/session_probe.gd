extends SceneTree

## The session gate: run a Practice session on the real kart on the real track, and
## measure the five things that have to be true for a lap time to mean anything.
##
##     godot --headless --path . --script tools/verify/session_probe.gd
##     godot --headless --path . --script tools/verify/session_probe.gd -- --ticks=40000
##
## ROADMAP M3c. `scripts/game/session_runner.gd`, `scripts/ui/timing_hud.gd` and
## `TrackLayout.project()` are what it exercises; `src/core/lap_timing.h` and
## `src/session/kart_session.cpp` are what it exercises them *against*, and **this is
## the first thing to feed either of those a real kart on a real track.** Both were
## written against unit tests and a synthetic drive helper, so the value of this file
## is mostly in the checks that could disagree with them.
##
## Named checks, PASS or FAIL with the measurement either way, a count, and a
## non-zero exit on any failure — `tuning_probe.gd`'s shape, including its rule that
## *a passing check still prints its measurement*, because a gate whose output is
## eleven identical words tells a reader nothing about what changed between two runs.
##
## ## Two halves, and only the second one needs an engine
##
## Checks 1 to 5 are arithmetic over `TrackLayout` and `KartLapTimer` and run before
## any scene is instantiated: the arc-length projection, the sector sum, and the
## state machine's transition table. They are the cheap ones and they gate the rest,
## because a driven lap measured through a broken projection is a number about
## nothing.
##
## Checks 6 to 11 drive `scenes/game/test_track.tscn` for real, through
## `KartBody.input_driver`. **No key is pressed and no action is read**, which is the
## same path `drive_probe.gd` uses and the reason a headless gate can drive at all.
##
## ## The pace driver is closed loop, and that is a departure worth naming
##
## `drive_probe.gd`'s scenarios are open-loop functions of the tick counter and its
## header explains at length why: *"a closed-loop scenario measures the controller as
## much as the kart, and two runs of a controller that reads its own noisy state are
## not guaranteed to agree"*. That argument is right and it is about **determinism**,
## which is what that file exists to check.
##
## This file cannot follow it. A lap of a 1,030 m circuit with an 11 m hairpin in it
## is not reachable by any fixed function of a tick counter — `track_layout.gd`'s own
## header says Turn 2 *"is built to fail"* and cannot be taken without crossing
## [#137](https://github.com/skiretic/kartgame/issues/137)'s cliff — so getting round
## needs something that looks at where the kart is. The consequences are stated
## rather than hidden:
##
##   * **This probe publishes no state hash and must never become a determinism
##     gate.** `tools/verify/drive.sh` is that gate and it is unaffected by anything
##     here.
##   * **The lap time it measures is a property of the kart and of this controller
##     together.** So it is judged against a *bracket* around an independently
##     computed prediction rather than against a figure, and the bracket's two ends
##     each carry their own argument — see `_check_lap_time`.
##   * The controller is deliberately dumb: a proportional steer onto a look-ahead
##     point and a target speed from the corner ahead. Making it quicker would make
##     the lap time a better lap time and a worse measurement.

const SCENE_PATH := "res://scenes/game/test_track.tscn"

## Physics ticks before the run is abandoned.
##
## 36,000 at 120 Hz is 300 s. The prediction below comes out near 60 s a lap and the
## driven lap is slower, so this is room for four laps at any pace the controller
## manages plus the deliberate off-track excursion. A run that hits this ceiling
## reports how far it got, which is the difference between "the kart is slow" and
## "the kart stopped".
const DEFAULT_TICKS := 36000

## Sector count the runner arms its timer with. Read from the runner rather than
## restated, because `SECTOR_COUNT` is a placeholder until ADR-0046's `track.json`
## authors real splits and a copy here would outlive it.

## The measured corner-speed table, from `tools/verify/drive_probe.gd`'s recorded
## steering-lock sweep against `KartBody`.
##
## **This is the only external number in the prediction and it is measured, not
## invented.** `scripts/track/track_layout.gd` carries the same six rows in its
## header and sized four corners from them; the two are the same table read twice,
## and it is quoted rather than derived because deriving a corner speed needs the
## tire's peak friction, the Ackermann solver and the load transfer, which is the
## whole vehicle model.
##
##     radius m   settles at km/h   lock   throttle
##       58.49        116.9         0.25     1.00
##       50.95        108.5         0.40     1.00
##       32.64         81.3         0.25     0.60
##       18.99         46.0         0.25     0.30
##        3.49         21.2         0.60     1.00
##        2.70          5.6         1.00     0.30
##
## Ascending in radius, because the interpolation below walks it in order.
const CORNER_RADIUS_M: PackedFloat64Array = [2.70, 3.49, 18.99, 32.64, 50.95, 58.49]
const CORNER_SPEED_KMH: PackedFloat64Array = [5.6, 21.2, 46.0, 81.3, 108.5, 116.9]

## How far ahead the pace driver aims, as seconds of travel and as a clamp in meters.
##
## Seconds rather than meters so the aim point moves out with speed, which is what
## keeps a proportional controller stable: at a fixed 10 m look-ahead the kart
## oscillates on a straight at 140 km/h and understeers into every corner. The clamps
## are the hairpin at one end — an aim point further ahead than the corner's own
## radius aims across it — and enough at the other end to see the Turn 1 kink coming.
const LOOK_SECONDS := 0.55
const LOOK_MIN_M := 6.0
const LOOK_MAX_M := 26.0

## How far ahead the pace driver looks for the corner that sets its target speed.
##
## A braking distance, and it is one: from 40 m/s to the hairpin's 10.6 m/s at
## `braking_g_min` needs (40^2 - 10.6^2) / (2 * 1.5 * 9.80665) = 50 m. 70 m is that
## with margin, because the controller brakes on a threshold rather than on a
## trajectory.
const CORNER_LOOKAHEAD_M := 70.0

## Proportional gains on the two steering errors, in units of lock per radian and per
## meter. Tuned by driving, and they are the controller rather than the kart — see
## the header on why making them better would make the measurement worse.
const STEER_GAIN_HEADING := 1.6
const STEER_GAIN_LATERAL := 0.09

## Throttle and brake either side of the target speed, and the band between them.
##
## A band rather than a single threshold, so the controller does not chatter between
## full throttle and full brake within one m/s of its target — which on this kart
## means shifting up and down through the gearbox several times a second.
const SPEED_BAND_MS := 1.2

## Lateral offset the excursion check aims for, meters.
##
## 6.0 m against the 4.0 m half width, so the whole kart is outside the line and not
## just the outer pair of wheels. `track_ribbon.gd` puts a 1.00 m curb outboard of the
## line, so 6.0 m is a meter clear of the curb and onto the grass.
const EXCURSION_LATERAL_M := 6.0

## Where on the lap the excursion happens, as arc length, and how fast it is driven.
##
## The first meters of the power straight out of the hairpin — `track_layout.gd`'s
## `SEG_POWER_STRAIGHT` starts at 420.665 m — because that is where the kart is
## slowest on the whole lap.
##
## **The speed cap is the part that had to be measured rather than assumed.** The
## window was first placed at 445-485 m on the reasoning that *"the hairpin dumps the
## kart out at 20-30 km/h"*, which is what `track_layout.gd` says about the corner
## exit. It is not what is true 25 m later: the trace measured **78.4 km/h at
## 444.7 m**, because the power straight exists to be accelerated down and the kart
## does that from the moment it unwinds the lock. Driving off at 78 km/h onto grass
## at 0.18 of asphalt's grip put the kart 33 m from the centerline and still going,
## which tests the tire model on grass and not the track-limits rule.
##
## So the window moved to the corner exit itself and the excursion is driven at a
## capped speed. That is a scenario choice and it is the honest one: the check is
## whether all four wheels outside the line strikes the lap out, and it should be
## driven at the speed that makes that unambiguous rather than at the speed that also
## spins the kart.
const EXCURSION_FROM_M := 424.0
const EXCURSION_TO_M := 452.0

## Speed the excursion is driven at, m/s. 8.0 m/s is 28.8 km/h — a pace the kart
## reaches in first out of the hairpin, and slow enough that grass is a surface it
## can steer back off rather than one it slides across.
const EXCURSION_SPEED_MS := 8.0

## The bracket the driven lap is judged in, as a multiple of the prediction. Each end
## carries its own argument and both are in `_check_lap_time`'s comment.
const PREDICTION_FLOOR := 0.85
const PREDICTION_CEILING := 1.80

## Two lap times agree exactly when they agree to within half a tick.
##
## The sector sum check is an equality over numbers that are all `ticks * step`, so it
## is exact in integers and only inexact in the double they are multiplied into. Half a
## tick is 4.2 ms and any real disagreement is a whole sector.
const HALF_TICK_S := 0.5 / 120.0

var _lines: Array[String] = []
var _passed := 0
var _failed := 0

var _layout: TrackLayout
var _root: Node
var _kart: KartBody
var _runner: SessionRunner

var _ticks := DEFAULT_TICKS
var _tick := 0

## The phases the driven half moves through. Not the runner's states — these are the
## probe's own script, and they are separate on purpose: a probe that reused the
## runner's state names would pass its own state-machine check by construction.
enum {
	PHASE_WAIT_GREEN,
	PHASE_CLEAN_LAPS,
	PHASE_EXCURSION,
	PHASE_RECOVER,
	PHASE_DONE,
}

var _phase := PHASE_WAIT_GREEN

## Every state the runner entered, in order, so check 9 reads the real sequence
## rather than asserting against the table twice.
var _visited: Array[int] = []

## The first fully timed lap: the second crossing, because the first is the out lap
## from a grid 30 m past the line.
var _timed_lap_s := -1.0
var _timed_sectors := PackedFloat64Array()
var _timed_lap_number := 0
var _timed_lap_valid := false
var _timed_lap_reason := ""

## The excursion's measurements.
var _worst_lateral := 0.0
var _reason_when_off := ""
var _off_at_distance := 0.0
var _off_at_lateral := 0.0
var _struck_out_reason := ""
var _struck_out_valid := true
var _struck_out_lap := 0

## Every crossing, printed under check 7 so a failure anywhere below can be read
## against what the kart actually did rather than against a single summary number.
var _lap_log: Array[String] = []

var _green_tick := 0
var _ran_out := false

## The centerline radius at each sample, meters, `INF` on a straight. Built once,
## because both the pace driver and the prediction walk it and neither should be
## deriving curvature from headings in a loop.
var _sample_radius := PackedFloat64Array()

## What the kart is handed this tick, and the projection hint the controller carries.
## Separate from the runner's hint on purpose: two consumers of one mutable cursor is
## how one of them ends up reading the other's tick.
var _input: Dictionary = {"throttle": 0.0, "brake": 0.0, "steer": 0.0}
var _pace_hint := -1.0
var _trace_every := 0

## Pace driver state, published so the report can say what the controller was doing
## when a check failed.
var _target_speed_ms := 0.0
var _lateral_bias := 0.0
var _distance := 0.0
var _lateral := 0.0
var _top_speed_ms := 0.0
var _brake_ms2 := 0.0
var _accel_ms2 := 0.0


func _initialize() -> void:
	var args := Cmdline.parse()
	_ticks = Cmdline.as_int(args, "ticks", DEFAULT_TICKS)
	_trace_every = Cmdline.as_int(args, "trace", 0)

	if not ClassDB.class_exists("KartLapTimer"):
		printerr("KartLapTimer is not registered — build the extension: "
			+ "scons arch=arm64 target=editor")
		quit(1)
		return

	_layout = TrackLayout.new()
	_build_radius_table()
	var reference: Dictionary = KartCore.kz_reference()
	_top_speed_ms = KartCore.kmh_to_ms(float(reference["top_speed_max_kmh"]))
	# The conservative end of both bands, so the prediction is the slow one. #131 is
	# open on whether §6.4's braking figure is a mean or a peak, so the minimum is the
	# only end of it that cannot be an overstatement.
	_brake_ms2 = float(reference["braking_g_min"]) * 9.80665
	# Averaged over the first 100 km/h, which is what the figure is. It understates
	# the launch and overstates the top end, and both errors make the prediction less
	# sharp rather than wrong in one direction.
	_accel_ms2 = KartCore.kmh_to_ms(100.0) / float(reference["zero_to_100_kmh_max_s"])

	_lines.append("=== session gate ======================================================")
	_lines.append("")
	_lines.append("    Five checks with no engine, then a Practice session driven on the")
	_lines.append("    test track through KartBody.input_driver. No key is pressed.")
	_lines.append("")

	_check_projection_at_samples()
	_check_projection_off_center()
	_check_projection_monotonic()
	_check_sector_sum_synthetic()
	_check_transition_table()
	_check_refusals()

	if _failed > 0:
		# The driven half is measured through the projection the checks above just
		# tested. Running it anyway would produce a lap time whose every input is
		# suspect, which is worse than not running it — that is the `SKIP_IMPORT=1`
		# trap's shape: a true-looking report about the wrong thing.
		_lines.append("")
		_lines.append("    the engine-free checks failed, so the driven session is skipped")
		_report()
		return

	var packed: PackedScene = load(SCENE_PATH)
	if packed == null:
		printerr("could not load " + SCENE_PATH)
		quit(1)
		return
	_root = packed.instantiate()
	get_root().add_child(_root)


## The centerline radius at every sample, from the walk's own headings.
##
## `radius = ds / |dh|`, which is the definition and is exact here because
## `TrackLayout._arc` placed the samples at equal angular steps. A straight has
## `dh == 0` exactly — `_straight` never touches `_heading` — so the test is an
## equality rather than a tolerance and `INF` means "not a corner" rather than
## "a very large corner".
##
## The heading difference is wrapped for the reason `_refine_on_arc` wraps its own:
## `_init` overwrites the closing sample with a copy of the first, so the raw
## difference across the last segment of the lap is +6.245 rad instead of -0.038 and
## would report the start/finish straight as a 4 mm radius hairpin.
func _build_radius_table() -> void:
	var count := _layout.samples.size() - 1
	_sample_radius.resize(count)
	for index in count:
		var from: Dictionary = _layout.samples[index]
		var to: Dictionary = _layout.samples[index + 1]
		var turn := wrapf(float(to["heading"]) - float(from["heading"]), -PI, PI)
		var step := float(to["distance"]) - float(from["distance"])
		_sample_radius[index] = INF if turn == 0.0 or step <= 0.0 else step / absf(turn)


# --- the arc-length projection --------------------------------------------------


## 1. Every centerline sample projects back to its own distance.
##
## The ground truth is exact rather than modeled, which is what makes this a
## measurement: `samples[i]["distance"]` is the arc length `_arc()` and `_straight()`
## computed analytically when they placed the sample, so the projection is being
## compared against the walk that built the track and not against a second estimate
## of it.
##
## Hinted from the previous sample, which is how the runner calls it — an unhinted
## sweep is check 2's job.
func _check_projection_at_samples() -> void:
	var worst := 0.0
	var worst_at := 0.0
	var worst_global := 0.0
	var worst_global_at := 0.0
	var hint := -1.0
	for sample in _layout.samples:
		var want: float = sample["distance"]
		var position: Vector3 = sample["position"]
		var got: Dictionary = _layout.project(position, hint)
		hint = got["distance"]
		var error := _wrapped_error(float(got["distance"]), want)
		if error > worst:
			worst = error
			worst_at = want
		# And again with no hint at all, which is the first tick of a session and the
		# tick after a respawn. A global search that disagreed with a hinted one would
		# mean the window is deciding the answer rather than narrowing it.
		var global: Dictionary = _layout.project(position, -1.0)
		var global_error := _wrapped_error(float(global["distance"]), want)
		if global_error > worst_global:
			worst_global = global_error
			worst_global_at = want
	_ok("projection at every sample", worst < 1e-3 and worst_global < 1e-3,
		"worst %.6f m at %.2f m hinted, %.6f m at %.2f m unhinted, over %d samples" % [
			worst, worst_at, worst_global, worst_global_at, _layout.samples.size(),
		])


## 2. And everywhere off the centerline, including through the hairpin.
##
## **The hairpin is the case this check exists for.** `right(heading)` at a point on
## an arc is the radial direction, so a sample offset that way is at the same angle
## about the same center and its true arc length is unchanged — an exactly known
## answer at every lateral offset, on every corner, with no model in between. Offsets
## out to 6 m cover the 8 m road, the 1 m curb and the grass beyond it.
##
## Turn 2 is reported separately because it is where the answer used to be wrong.
## Before `TrackLayout._refine_on_arc` existed the chord interpolation was out by
## `lateral * segment_turn / 2`, measured at 0.204 m at 3.5 m of offset on the 11 m
## radius, and a fifth of a meter is both a sector mark firing early and an arc length
## that steps backwards — which `lap_timing.h` reads as a lap.
func _check_projection_off_center() -> void:
	var worst := 0.0
	var worst_at := 0.0
	var worst_offset := 0.0
	var worst_lateral := 0.0
	var worst_hairpin := 0.0
	var hairpin_from := _layout.segment_start(TrackLayout.SEG_HAIRPIN)
	var hairpin_to := _layout.segment_end(TrackLayout.SEG_HAIRPIN)
	for offset in [-6.0, -4.0, -2.0, 2.0, 4.0, 6.0]:
		var hint := -1.0
		for sample in _layout.samples:
			var want: float = sample["distance"]
			var position: Vector3 = (sample["position"] as Vector3) \
				+ TrackLayout.right(sample["heading"]) * offset
			var got: Dictionary = _layout.project(position, hint)
			hint = got["distance"]
			var error := _wrapped_error(float(got["distance"]), want)
			if error > worst:
				worst = error
				worst_at = want
				worst_offset = offset
			if want >= hairpin_from and want <= hairpin_to:
				worst_hairpin = maxf(worst_hairpin, error)
			# The lateral it hands back is the offset that was applied, and it has to be,
			# because the off-track rule is a comparison on exactly this number against
			# half the track width.
			worst_lateral = maxf(worst_lateral, absf(float(got["lateral"]) - offset))
	_ok("projection off the centerline", worst < 1e-3 and worst_lateral < 1e-3,
		"worst %.6f m at %.2f m on a %+.1f m offset; hairpin worst %.6f m; lateral worst %.6f m" % [
			worst, worst_at, worst_offset, worst_hairpin, worst_lateral,
		])


## 3. Arc length never steps backwards while the kart drives forwards.
##
## Separate from checks 1 and 2 because it is a different property and it is the one
## the lap timer depends on: `LapTimer::cross_marks` reads a *fall* in arc length as
## the forward crossing of the start line, so a projection that jitters backwards
## anywhere is a projection that can count a lap in the middle of a corner. A worst
## error inside tolerance does not imply monotonic — the 0.204 m secant bias was a
## sawtooth that reversed sign at every sample.
func _check_projection_monotonic() -> void:
	var worst_step := 0.0
	var backward := 0
	var total := _layout.length()
	for offset in [-3.5, 0.0, 3.5]:
		var hint := -1.0
		var previous := -1.0
		for sample in _layout.samples:
			var position: Vector3 = (sample["position"] as Vector3) \
				+ TrackLayout.right(sample["heading"]) * offset
			var got: Dictionary = _layout.project(position, hint)
			var distance: float = got["distance"]
			hint = distance
			if previous >= 0.0:
				var step := distance - previous
				if step < -total * 0.5:
					step += total
				if step < 0.0:
					backward += 1
					worst_step = minf(worst_step, step)
			previous = distance
	_ok("arc length is monotonic forward", backward == 0,
		"%d backward steps over 3 offsets, worst %.6f m" % [backward, worst_step])


# --- the lap timer --------------------------------------------------------------


## 4. Sector times sum to the lap time exactly, with no engine involved.
##
## Driven synthetically at a constant arc-length rate, which is the one way to get a
## lap out of `KartLapTimer` where every tick is accounted for by construction. The
## driven lap gets the same check in check 8; this one is here because it isolates the
## timer from the projection, the kart and the controller, so a failure has one
## possible cause.
##
## The sum is exact in *ticks* — `LapTimer::complete_lap` writes every sector as
## `sector_ticks_at_mark_[i] * step_s` and `lap_ticks_` is their sum — so the only
## inexactness available is the double the tick counts are multiplied into.
func _check_sector_sum_synthetic() -> void:
	var step := 1.0 / float(Engine.physics_ticks_per_second)
	var timer := KartLapTimer.new()
	if not timer.begin_even(_layout.length(), SessionRunner.SECTOR_COUNT, step):
		_ok("sector sum, driven synthetically", false, "begin_even refused the layout")
		return

	# 20 m/s, from 30 m along — `test_track.gd`'s grid — so the first crossing is the
	# out lap the real session has and the second is a full lap.
	var speed := 20.0
	var distance := 30.0
	var laps: Array[float] = []
	var sums: Array[float] = []
	var worst := 0.0
	for tick in 20000:
		distance = fmod(distance + speed * step, _layout.length())
		if timer.advance(distance, false):
			var sum := 0.0
			for sector in timer.last_sectors():
				sum += sector
			laps.append(timer.last_time())
			sums.append(sum)
			worst = maxf(worst, absf(sum - timer.last_time()))
			if laps.size() >= 3:
				break
	var detail := ""
	if laps.size() < 3:
		detail = "only %d laps in 20,000 ticks" % laps.size()
	else:
		detail = "%d laps, worst |sum - lap| %.9f s; lap 2 %s = %s" % [
			laps.size(), worst, SessionRunner.format_time(laps[1]),
			" + ".join(_sector_strings(sums[1], timer)),
		]
	_ok("sector sum, driven synthetically", laps.size() >= 3 and worst <= HALF_TICK_S, detail)


func _sector_strings(_sum: float, timer: KartLapTimer) -> PackedStringArray:
	var out := PackedStringArray()
	for sector in timer.last_sectors():
		out.append("%.4f" % sector)
	return out


# --- the state machine ----------------------------------------------------------


## 5. The transition table cannot skip a state or run one twice.
##
## Checked as a property of `SessionRunner.TRANSITIONS` rather than by trying illegal
## moves, because `_enter` is private and a probe that reached into it would be
## testing a different function than the one the runner uses. What the table has to
## hold:
##
##   * **no state lists itself**, which is what "cannot run one twice" means for a
##     machine that only moves through this table;
##   * **`SETUP` reaches neither `GREEN` nor `RESULT`**, so a session cannot start
##     driving without being placed and held, and cannot produce a result without
##     ever having run;
##   * every state is reachable from `SETUP`, so none of the six is decoration;
##   * the two terminal states go nowhere.
##
## Check 9 is the other half and it is the one that catches a machine whose table is
## right and whose code does not use it: it reads the sequence a real driven session
## actually visited.
func _check_transition_table() -> void:
	var problems := PackedStringArray()
	var table: Dictionary = SessionRunner.TRANSITIONS
	if table.size() != SessionRunner.STATE_COUNT:
		problems.append("%d rows for %d states" % [table.size(), SessionRunner.STATE_COUNT])
	for state in table:
		var to: Array = table[state]
		if to.has(state):
			problems.append("%s reaches itself" % SessionRunner.STATE_NAMES[state])
	var from_setup: Array = table[SessionRunner.STATE_SETUP]
	if from_setup.has(SessionRunner.STATE_GREEN):
		problems.append("setup reaches green without being held")
	if from_setup.has(SessionRunner.STATE_RESULT):
		problems.append("setup reaches result without running")
	if not table[SessionRunner.STATE_RESULT].is_empty():
		problems.append("result is not terminal")
	if not table[SessionRunner.STATE_REFUSED].is_empty():
		problems.append("refused is not terminal")

	# Breadth-first from SETUP, so an unreachable state is a failure rather than an
	# unused row.
	var reached := {SessionRunner.STATE_SETUP: true}
	var frontier: Array[int] = [SessionRunner.STATE_SETUP]
	while not frontier.is_empty():
		var state: int = frontier.pop_back()
		for next: int in table[state]:
			if not reached.has(next):
				reached[next] = true
				frontier.append(next)
	if reached.size() != SessionRunner.STATE_COUNT:
		problems.append("%d of %d states reachable from setup" % [
			reached.size(), SessionRunner.STATE_COUNT,
		])

	_ok("transition table", problems.is_empty(),
		"%d states, %d reachable from setup, %s" % [
			SessionRunner.STATE_COUNT, reached.size(),
			"no self-transitions and both terminals are terminal" if problems.is_empty()
				else ", ".join(problems),
		])


## 6. A session that cannot be run is refused, and says which field.
##
## The three refusals are the ones `_first_problem()` names, and each is a **"this is
## not built"** rather than a limitation being hidden. Checked because
## `GAMEDESIGN.md` §13's standard is that a stubbed mode reads worse than an absent
## one, and a runner that quietly ran a Heat with one kart and called it a Heat would
## be the stub.
##
## Also checks the two guards that stop a state running twice, which *are* directly
## reachable: `configure()` and `begin()` both refuse outside `SETUP`.
func _check_refusals() -> void:
	var problems := PackedStringArray()
	var refusals := PackedStringArray()

	# No configuration at all.
	var bare := SessionRunner.new()
	if bare.configure(null, null, null, null):
		problems.append("a null configuration was accepted")
	if bare.state() != SessionRunner.STATE_REFUSED:
		problems.append("a null configuration did not refuse")
	refusals.append(bare.refusal())
	# And the guard: `begin()` outside SETUP.
	if bare.begin():
		problems.append("begin() succeeded from refused")
	if bare.configure(null, null, null, null):
		problems.append("configure() succeeded from refused")

	# **The other two need a kart and a driver that are not null**, and getting that
	# wrong is what the first run of this check did: passing null for all three made
	# every case report the null guard's own sentence, so the check passed while
	# testing one refusal three times. Detached nodes are enough — the refusal path
	# returns before `configure()` touches the physics — and they are freed below
	# because a `Node` is not reference counted and this probe would otherwise leak
	# one per case.
	var kart := KartBody.new()
	var driver := PlayerDriver.new()

	# An entry list, which needs a field there is none of.
	var field_session := KartSession.new()
	field_session.set_track("test_track")
	field_session.set_type(KartSession.TYPE_HEAT)
	field_session.use_scheduled_limit()
	field_session.set_entry_count(8)
	var field_runner := SessionRunner.new()
	field_runner.configure(field_session, kart, driver, _layout)
	refusals.append(field_runner.refusal())
	if not field_runner.refusal().contains("field"):
		problems.append("an entry list of 8 was not refused for having no field")

	# A class nothing simulates.
	var ok_session := KartSession.new()
	ok_session.set_track("test_track")
	ok_session.set_kart_class(KartSession.CLASS_OK)
	var ok_runner := SessionRunner.new()
	ok_runner.configure(ok_session, kart, driver, _layout)
	refusals.append(ok_runner.refusal())
	if not ok_runner.refusal().contains("ok"):
		problems.append("the OK class was not refused by name")

	# A rate the engine will not run at. Issue #174 put the tick rate in the
	# configuration hash; a config claiming a rate the engine will not integrate
	# at would record a replay whose header lies about its own integration.
	var claimed_hz := 240 if Engine.physics_ticks_per_second != 240 else 120
	var rate_session := KartSession.new()
	rate_session.set_track("test_track")
	rate_session.set_tick_hz(claimed_hz)
	var rate_runner := SessionRunner.new()
	rate_runner.configure(rate_session, kart, driver, _layout)
	refusals.append(rate_runner.refusal())
	if not rate_runner.refusal().contains("%d Hz" % claimed_hz):
		problems.append("a %d Hz claim against a %d Hz engine was not refused by name" % [
			claimed_hz, Engine.physics_ticks_per_second,
		])

	for refusal in refusals:
		if refusal == "":
			problems.append("a refused session gave no reason")

	for node in [bare, field_runner, ok_runner, rate_runner, driver, kart]:
		node.free()

	_ok("refusals name the field", problems.is_empty(),
		"%s" % ("; ".join(refusals) if problems.is_empty() else ", ".join(problems)))


# --- the driven session ---------------------------------------------------------


## The kart and the runner, found on the first tick rather than in `_initialize`.
##
## A `--script` main loop parents its scene before the tree starts running, so
## `_ready` has not run and there are no children to find. `drive_probe.gd` and
## `shoot.gd` both have this shape and CLAUDE.md records it as a trap.
func _attach() -> bool:
	_kart = _root.find_child("Kart", false, false) as KartBody
	_runner = _root.find_child("SessionRunner", false, false) as SessionRunner
	if _kart == null:
		printerr("no Kart in the scene — is assets/generated/kart.glb built?")
		return false
	if _runner == null:
		printerr("no SessionRunner in the scene — test_track.gd should have built one")
		return false
	if _runner.state() == SessionRunner.STATE_REFUSED:
		printerr("the scene's session was refused: " + _runner.refusal())
		return false
	_runner.state_changed.connect(_on_state_changed)
	_runner.lap_completed.connect(_on_lap_completed)
	_visited.append(_runner.state())
	# **The Callable overrides the scene's `PlayerDriver`**, which is what makes this
	# a headless gate rather than a keyboard test — `KartBody::gather_input` gives an
	# `input_driver` precedence over pushed input, and its comment records what
	# happened when the ordering was the other way: all six `drive.sh` scenarios
	# agreed on one hash because the scenario's Callable was never consulted.
	_kart.input_driver = _pace_driver
	return true


func _on_state_changed(_from: int, to: int) -> void:
	_visited.append(to)


## Every start-line crossing, from the runner's own signal rather than by polling
## `laps_completed()`.
##
## Polling would read the count one tick late and, worse, would read `last_time()`
## after the next lap had begun overwriting the state it describes. The signal is
## emitted from inside the crossing tick, which is the only moment `last()` and the
## lap that produced it are the same thing.
func _on_lap_completed(number: int, time_s: float, valid: bool, reason: String) -> void:
	_lap_log.append("lap %d %s %s%s" % [
		number, SessionRunner.format_time(time_s), "valid" if valid else "STRUCK OUT",
		"" if valid else " (" + reason + ")",
	])
	match _phase:
		PHASE_CLEAN_LAPS:
			# The **second** crossing. `test_track.gd` puts the grid 30 m past the line,
			# so the first is an out lap of 1,000 m from a standing start and
			# `lap_timing.h` marks it `OutLap` precisely so it cannot become a best.
			if number >= 2:
				_timed_lap_s = time_s
				_timed_sectors = _runner.timer().last_sectors()
				_timed_lap_number = number
				_timed_lap_valid = valid
				_timed_lap_reason = reason
				_phase = PHASE_EXCURSION
		PHASE_RECOVER:
			_struck_out_reason = reason
			_struck_out_valid = valid
			_struck_out_lap = number
			_phase = PHASE_DONE


func _physics_process(_delta: float) -> bool:
	if _kart == null and not _attach():
		_lines.append("")
		_lines.append("    the scene did not come up, so nothing below was driven")
		_report()
		return true

	_tick += 1
	_advance_phase()
	_compute_input()
	_trace()

	if _phase == PHASE_DONE:
		_runner.end_session("probe finished")
		_run_driven_checks()
		_report()
		return true
	if _tick >= _ticks:
		_ran_out = true
		_runner.end_session("probe ran out of ticks")
		_run_driven_checks()
		_report()
		return true
	return false


## Move the probe's own script along. See `_phase` on why this is not the runner's
## state machine.
func _advance_phase() -> void:
	match _phase:
		PHASE_WAIT_GREEN:
			if _runner.state() == SessionRunner.STATE_GREEN:
				_green_tick = _tick
				_phase = PHASE_CLEAN_LAPS
		PHASE_EXCURSION:
			# Aim off the road across the window, and only across it. The lap is already
			# spoiled the moment all four wheels are outside, so what the rest of the lap
			# tests is that the taint *survives* — `LapTimer::taint` keeps the first
			# reason and clears it only when a lap completes, and a rule that healed on
			# rejoining would strike out nothing.
			_lateral_bias = EXCURSION_LATERAL_M \
				if _distance >= EXCURSION_FROM_M and _distance <= EXCURSION_TO_M else 0.0
			if _distance >= EXCURSION_FROM_M:
				_worst_lateral = maxf(_worst_lateral, absf(_lateral))
				var reason := _runner.timer().current_reason()
				if _reason_when_off == "" and reason != "valid":
					_reason_when_off = reason
					_off_at_distance = _distance
					_off_at_lateral = _lateral
			# Back on the road, and only once the kart is actually back: the phase is
			# what re-arms the speed cap, so leaving it on distance alone would hand the
			# kart full throttle while it is still three meters onto the grass.
			if _distance > EXCURSION_TO_M and absf(_lateral) < 1.0:
				_lateral_bias = 0.0
				_phase = PHASE_RECOVER


## `--trace=60` prints a line every 60 ticks: where the kart is, what the controller
## asked for, and what the timer thinks.
##
## Not decoration. Every check below reports one number, and when one of them fails
## the question is always the same — *what was the kart doing* — and answering it
## without this means adding prints and running again. The first run of this file
## reported 29.5 m covered in 24.8 s across five failures, and the trace is what said
## the kart was driving in a circle rather than standing still.
func _trace() -> void:
	if _trace_every <= 0 or _tick % _trace_every != 0:
		return
	print("t%6d  %-6s ph%d  d %7.1f lat %+6.2f  %5.1f km/h -> %5.1f  thr %.2f brk %.2f str %+.2f  gear %d  %s"
		% [
			_tick, _runner.state_name(), _phase, _distance, _lateral,
			KartCore.ms_to_kmh(_kart.speed_ms), KartCore.ms_to_kmh(_target_speed_ms),
			float(_input["throttle"]), float(_input["brake"]), float(_input["steer"]),
			_kart.get_gear(), _runner.timer().current_reason(),
		])


# --- the pace driver ------------------------------------------------------------


## What the kart is handed this tick. Held in a member and returned by a Callable,
## because `KartBody::gather_input` calls the Callable during the *kart's* physics
## step and this runs during the `SceneTree`'s, which is earlier — `drive_probe.gd`
## records the same ordering.
func _pace_driver() -> Dictionary:
	return _input


## A proportional steer onto a look-ahead point, and a target speed from the corner
## ahead. Deliberately dumb; see the header on why a better driver is a worse
## measurement.
func _compute_input() -> void:
	if not _runner.is_running():
		# Neutral while held and after the flag. Handing the kart throttle during
		# `HELD` would be testing that the input gate leaks.
		_input = {"throttle": 0.0, "brake": 0.0, "steer": 0.0}
		return

	var placed := _layout.project(_kart.global_position, _pace_hint)
	_pace_hint = placed["distance"]
	_distance = placed["distance"]
	_lateral = placed["lateral"]

	var speed: float = _kart.speed_ms
	var total := _layout.length()

	# --- where to aim -------------------------------------------------------------
	var look := clampf(speed * LOOK_SECONDS, LOOK_MIN_M, LOOK_MAX_M)
	var aim := _layout.nearest_sample(fposmod(_distance + look, total))
	var aim_point: Vector3 = (aim["position"] as Vector3) \
		+ TrackLayout.right(aim["heading"]) * _lateral_bias

	var to_aim := aim_point - _kart.global_position
	to_aim.y = 0.0
	var forward := -_kart.global_transform.basis.z
	forward.y = 0.0

	# **The sign, and it was wrong first, in the direction that looks like a physics
	# bug.** `src/core/steering.h` states the convention: *"steer angle positive turns
	# the wheel to the left"*, and `drive_probe.gd` pins it independently by driving
	# its `skidpad_right` scenario on a **negative** lock. `signed_angle_to` is
	# positive counter-clockwise about `UP` and Godot's forward is -Z, so a target to
	# the kart's right gives a negative angle —
	# `Vector3.FORWARD.signed_angle_to(Vector3.RIGHT, Vector3.UP)` is -PI/2 — which is
	# already the sign the solver wants. **No negation.**
	#
	# Negating it here made the controller drive away from its aim point: the kart
	# left the road at Turn 1 and the trace showed lateral offset climbing through
	# +7 m, +13 m, +19 m with the steering saturated and reversing every few ticks.
	# It reads exactly like an unstable vehicle and it is a sign error in the probe.
	var heading_error := 0.0
	if to_aim.length_squared() > 1e-6 and forward.length_squared() > 1e-6:
		heading_error = forward.signed_angle_to(to_aim, Vector3.UP)

	# Same convention for the lateral term: the kart being *right* of where it should
	# be is a positive `_lateral`, and coming back means turning left, which is
	# positive lock.
	var steer := heading_error * STEER_GAIN_HEADING \
		+ (_lateral - _lateral_bias) * STEER_GAIN_LATERAL

	# --- how fast --------------------------------------------------------------
	_target_speed_ms = _corner_speed_ms(_tightest_radius_ahead(_distance))
	# Capped across the excursion and the rejoin after it. See `EXCURSION_SPEED_MS`.
	if _phase == PHASE_EXCURSION and _distance >= EXCURSION_FROM_M:
		_target_speed_ms = minf(_target_speed_ms, EXCURSION_SPEED_MS)
	var throttle := 0.0
	var brake := 0.0
	if speed < _target_speed_ms - SPEED_BAND_MS:
		throttle = 1.0
	elif speed > _target_speed_ms + SPEED_BAND_MS:
		# Proportional rather than full, so the controller does not stand the kart on
		# its nose a hundred meters early and arrive at the corner at half the speed it
		# was aiming for — which would measure the controller's pessimism as the kart's
		# pace.
		brake = clampf((speed - _target_speed_ms) / 8.0, 0.15, 1.0)
	else:
		# Enough to hold the speed against drag and corner scrub, which is what the
		# sweep's own rows were measured on.
		throttle = 0.35

	_input = {
		"throttle": throttle,
		"brake": brake,
		"steer": clampf(steer, -1.0, 1.0),
	}


## The tightest centerline radius within `CORNER_LOOKAHEAD_M` ahead, meters.
func _tightest_radius_ahead(from_distance: float) -> float:
	var total := _layout.length()
	var tightest := INF
	var count := _sample_radius.size()
	var index := _sample_index_at(from_distance)
	var covered := 0.0
	while covered < CORNER_LOOKAHEAD_M:
		tightest = minf(tightest, _sample_radius[index])
		var next := (index + 1) % count
		var step: float = float(_layout.samples[index + 1]["distance"]) \
			- float(_layout.samples[index]["distance"])
		covered += step
		index = next
		if index == 0 and covered > total:
			break
	return tightest


func _sample_index_at(distance: float) -> int:
	var low := 0
	var high := _sample_radius.size() - 1
	while low < high:
		var middle := (low + high + 1) / 2
		if float(_layout.samples[middle]["distance"]) <= distance:
			low = middle
		else:
			high = middle - 1
	return low


## Speed the kart holds at a given radius, m/s, from the measured sweep.
##
## Interpolated in **log radius**, because the six measured rows span 2.70 m to
## 58.49 m — a factor of twenty-two — and a linear interpolation across that puts
## almost every corner on this track inside one interval. Log spacing distributes the
## rows the way the measurement did.
##
## Above the widest measured row the kart is not cornering-limited and the cap is its
## own top speed; below the tightest it is `5.6 km/h`, which is what full lock
## measured and is `#137` stated as a number.
func _corner_speed_ms(radius: float) -> float:
	if radius >= CORNER_RADIUS_M[CORNER_RADIUS_M.size() - 1]:
		return _top_speed_ms
	if radius <= CORNER_RADIUS_M[0]:
		return KartCore.kmh_to_ms(CORNER_SPEED_KMH[0])
	for index in range(1, CORNER_RADIUS_M.size()):
		if radius > CORNER_RADIUS_M[index]:
			continue
		var low := log(CORNER_RADIUS_M[index - 1])
		var high := log(CORNER_RADIUS_M[index])
		var travel := (log(radius) - low) / (high - low)
		return KartCore.kmh_to_ms(
			CORNER_SPEED_KMH[index - 1]
			+ travel * (CORNER_SPEED_KMH[index] - CORNER_SPEED_KMH[index - 1]))
	return _top_speed_ms


# --- the prediction -------------------------------------------------------------


## What the geometry and the kart's measured pace predict for a lap, seconds.
##
## A quasi-static lap simulation over the centerline, which is the standard
## construction and is three passes:
##
##   1. **A corner speed at every sample**, from the measured sweep against the
##      radius the centerline actually has there, capped at the measured top speed.
##   2. **A backward pass**, so the kart is already slow enough at the corner
##      entry — `v[i] <= sqrt(v[i+1]^2 + 2 a ds)` with `a` the measured braking
##      figure.
##   3. **A forward pass**, so it cannot leave a corner faster than it can
##      accelerate — the same inequality with the measured acceleration.
##
## Both passes are run three times, because the lap is a closed loop and one pass
## carries information only in one direction: the speed at the start line depends on
## Turn 4 behind it, which depends on the approach straight before that.
##
## **Every constant in it is measured and none is from this file.** The corner speeds
## are `drive_probe.gd`'s recorded sweep, the top speed and both longitudinal figures
## come out of `KartCore.kz_reference()`, and the geometry is `TrackLayout`'s own
## walk. What it is *not* is a racing line: the centerline is the tightest path
## through every corner, so this is a slow prediction by construction, and that is
## the direction an honest bound should err in.
func _predict_lap_s() -> float:
	var count := _sample_radius.size()
	var speed := PackedFloat64Array()
	var step := PackedFloat64Array()
	speed.resize(count)
	step.resize(count)
	for index in count:
		speed[index] = _corner_speed_ms(_sample_radius[index])
		step[index] = float(_layout.samples[index + 1]["distance"]) \
			- float(_layout.samples[index]["distance"])

	for _pass in 3:
		for index in range(count - 1, -1, -1):
			var ahead: float = speed[(index + 1) % count]
			speed[index] = minf(speed[index], sqrt(ahead * ahead + 2.0 * _brake_ms2 * step[index]))
	for _pass in 3:
		for index in count:
			var behind: float = speed[index]
			var next := (index + 1) % count
			speed[next] = minf(speed[next],
				sqrt(behind * behind + 2.0 * _accel_ms2 * step[index]))

	var seconds := 0.0
	for index in count:
		var mean: float = (speed[index] + speed[(index + 1) % count]) * 0.5
		if mean > 0.01:
			seconds += step[index] / mean
	return seconds


# --- the driven checks ----------------------------------------------------------


func _run_driven_checks() -> void:
	_lines.append("")
	_check_lap_completed()
	_check_lap_time()
	_check_sector_sum_driven()
	_check_state_sequence()
	_check_off_track()
	_check_result()


## 7. The kart completed a lap of the test track under its own power.
func _check_lap_completed() -> void:
	var timer := _runner.timer()
	_ok("a lap was completed", _timed_lap_s > 0.0 and not _ran_out,
		"%d crossings, %d counted, %d struck out, %.1f m covered in %s%s" % [
			timer.laps_completed(), timer.valid_lap_count(), timer.invalid_lap_count(),
			_runner.odometer_m(), SessionRunner.format_time(_runner.elapsed_s()),
			"   RAN OUT OF TICKS" if _ran_out else "",
		])
	for entry in _lap_log:
		_lines.append("      %s" % entry)


## 8. The lap time is inside a bracket the geometry and the measured pace predict.
##
## **A bracket and not a figure, and the reason is in the header:** the lap was driven
## by a proportional controller, so the time is a property of the kart *and* of that
## controller. What can be argued is a range, and each end of it is argued separately.
##
## **The floor is not a tolerance at all — it is arithmetic.** A lap cannot be quicker
## than the lap length divided by the kart's measured top speed, because that is a lap
## driven flat out in a straight line through four corners. Nothing about the
## controller can move it. It is checked separately from the bracket for that reason:
## a failure there is a broken projection or a broken timer, never a slow driver.
##
## **The bracket's lower end** is 0.85 of the prediction. The prediction drives the
## centerline, which is the tightest line through every corner, so a real line can be
## quicker — but only by widening the corners, and the two 180 degree corners are 207 m
## of the 1,030 m lap. A lap 15% under the prediction would mean the corner-speed
## table is too pessimistic to be a prediction, which is a finding about the sweep
## rather than about this session.
##
## **The upper end is a regression bound and not a first-principles one, and saying
## so is the honest version.** It cannot be argued from the kart, because how far
## above the prediction a lap lands is a property of this controller: it brakes on a
## threshold rather than a trajectory, does not use the road width, and has to get
## round an 11 m hairpin that `track_layout.gd` says *"is built to fail"*. The
## measured ratio is **1.15**, so 1.80 is that with 57% of headroom — room for the
## controller to be worse than it is today without the gate crying wolf, and still
## far under the 3x and up that a spin, a beaching or a crawl produces. Its job is to
## catch a kart that stopped, not to certify a lap time.
##
## The ratio is printed either way, so the number a reader should be looking at is in
## the output rather than encoded in a pass.
func _check_lap_time() -> void:
	var predicted := _predict_lap_s()
	var floor_s := _layout.length() / _top_speed_ms
	var ratio := _timed_lap_s / predicted if predicted > 0.0 else 0.0
	var inside := _timed_lap_s >= floor_s \
		and ratio >= PREDICTION_FLOOR and ratio <= PREDICTION_CEILING
	_ok("lap time against the prediction", _timed_lap_s > 0.0 and inside,
		"lap %d %s against %s predicted, ratio %.2f (band %.2f-%.2f); hard floor %s" % [
			_timed_lap_number, SessionRunner.format_time(_timed_lap_s),
			SessionRunner.format_time(predicted), ratio,
			PREDICTION_FLOOR, PREDICTION_CEILING, SessionRunner.format_time(floor_s),
		])
	_lines.append("      prediction from %.1f km/h top, %.2f m/s^2 accel, %.2f m/s^2 brake"
		% [KartCore.ms_to_kmh(_top_speed_ms), _accel_ms2, _brake_ms2])


## 9. The driven lap's sectors sum to the driven lap's time, exactly.
##
## Check 4 proved the timer does this on a synthetic drive. This proves it survives a
## real one, where the arc length arrives from a projection rather than from an
## integer and the marks are crossed at whatever speed the kart happened to be doing.
func _check_sector_sum_driven() -> void:
	var sum := 0.0
	var parts := PackedStringArray()
	for sector in _timed_sectors:
		sum += sector
		parts.append("%.4f" % sector)
	var error := absf(sum - _timed_lap_s)
	_ok("sector sum on the driven lap", _timed_sectors.size() > 0 and error <= HALF_TICK_S,
		"%s = %s, |sum - lap| %.9f s over %d sectors" % [
			" + ".join(parts), SessionRunner.format_time(sum), error, _timed_sectors.size(),
		])


## 10. The session visited every state once, in order, and skipped none.
##
## The other half of check 5, and the one that catches a machine whose table is right
## and whose code does not consult it. The sequence a Practice session produces is
## `setup -> held -> green -> result`: `FLAGGED` is absent and that is correct rather
## than a skip, because an `Open` limit never flags — `GAMEDESIGN.md` §4 gives Practice
## no limit at all, so nothing inside the simulation can raise a chequered flag.
##
## **`SETUP` is inferred and the rest is observed, and the difference is stated
## because the first version of this check asserted the wrong thing and failed.** A
## `--script` main loop cannot see `SETUP`: the scene's `_ready` runs `configure()`
## and `begin()` while the probe is still in `_initialize`, so by the first physics
## tick — the earliest a probe can find the node and connect to its signal — the
## runner has already left it. Asserting on an unobservable state would have meant
## either weakening the check or having the scene not start its own session, and both
## are worse than saying which link is inferred.
##
## What is observed still pins the machine, because the missing link is the one the
## table already forbids getting wrong: **the first state seen is `HELD`**, which is
## reachable from `SETUP` and from nothing else, so the run cannot have skipped to
## `GREEN`. On top of that, the sequence is a **path through the table**, no state
## appears **twice**, and it **ends terminal**.
func _check_state_sequence() -> void:
	var names := PackedStringArray()
	for state in _visited:
		names.append(SessionRunner.STATE_NAMES[state])

	var problems := PackedStringArray()
	var seen := {}
	for state in _visited:
		if seen.has(state):
			problems.append("%s entered twice" % SessionRunner.STATE_NAMES[state])
		seen[state] = true
	for index in range(1, _visited.size()):
		var from: int = _visited[index - 1]
		var to: int = _visited[index]
		if not (SessionRunner.TRANSITIONS[from] as Array).has(to):
			problems.append("%s -> %s is not in the table" % [
				SessionRunner.STATE_NAMES[from], SessionRunner.STATE_NAMES[to],
			])
	# The inferred first link, asserted as far as it can be: `HELD` has exactly one
	# predecessor in the table, so finding the runner there means it came from `SETUP`.
	if _visited.is_empty() or _visited[0] != SessionRunner.STATE_HELD:
		problems.append("first observed state was not held")
	var into_held := PackedStringArray()
	for state: int in SessionRunner.TRANSITIONS:
		if (SessionRunner.TRANSITIONS[state] as Array).has(SessionRunner.STATE_HELD):
			into_held.append(SessionRunner.STATE_NAMES[state])
	if into_held.size() != 1 or into_held[0] != "setup":
		problems.append("held is reachable from %s, so the inferred link is not unique"
			% ", ".join(into_held))
	if _visited.is_empty() or _visited[_visited.size() - 1] != SessionRunner.STATE_RESULT:
		problems.append("did not end in result")

	_ok("state sequence", problems.is_empty(),
		"(setup) -> %s; held reachable only from setup, so the unobserved link is pinned%s" % [
			" -> ".join(names),
			"" if problems.is_empty() else "   " + ", ".join(problems),
		])


## 11. Driving off the track strikes the lap out, and says which rule did it.
##
## Two separate assertions and both matter. That the lap in progress is reported
## spoiled **while it is happening** — a driver has to know there is no point finishing
## it — and that the reason is `off_track` and not `missed_mark`, because
## `LapTimer::taint` keeps the *first* reason and a projection that jumped during the
## excursion would report the wrong one. And that the taint **survives the rejoin**:
## the kart is steered back and drives most of a lap clean, and the lap still has to be
## struck out at the line.
func _check_off_track() -> void:
	var problems := PackedStringArray()
	if _reason_when_off != "off_track":
		problems.append("reason while off was '%s'" % _reason_when_off)
	if _worst_lateral <= TrackRibbon.TRACK_WIDTH * 0.5:
		problems.append("never got past the line: worst %.2f m" % _worst_lateral)
	if _struck_out_valid:
		problems.append("the lap counted anyway")
	if _struck_out_reason != "off_track":
		problems.append("struck out as '%s'" % _struck_out_reason)
	_ok("off track strikes the lap out", problems.is_empty(),
		"reached %.2f m from the centerline against a %.1f m half width at %.1f m; "
		% [_worst_lateral, TrackRibbon.TRACK_WIDTH * 0.5, _off_at_distance]
		+ "lap %d struck out as '%s'%s" % [
			_struck_out_lap, _struck_out_reason,
			"" if problems.is_empty() else "   " + ", ".join(problems),
		])


## 12. A session that ended produced a result that says what it produced.
##
## The keys checked are the ones a profile writer and a replay header need, and they
## are checked for being **present and consistent with the timer** rather than for
## being non-empty: a result that reported a best lap the timer does not have is worse
## than one that reported none.
func _check_result() -> void:
	var result := _runner.result()
	var timer := _runner.timer()
	var problems := PackedStringArray()
	for key in [
		"type_name", "config_hash", "outcome", "elapsed_s", "laps_completed",
		"distance_m", "best_lap_s", "has_best", "valid_laps", "invalid_laps",
	]:
		if not result.has(key):
			problems.append("no '%s'" % key)
	if problems.is_empty():
		if bool(result["has_best"]) != timer.has_best():
			problems.append("has_best disagrees with the timer")
		if absf(float(result["best_lap_s"]) - timer.best_time()) > HALF_TICK_S:
			problems.append("best_lap_s disagrees with the timer")
		if int(result["laps_completed"]) != timer.laps_completed():
			problems.append("laps_completed disagrees with the timer")
		if String(result["config_hash"]) == "":
			problems.append("no config hash")
		if String(result["outcome"]) == "":
			problems.append("no outcome")
	_ok("the result says what it produced", problems.is_empty(),
		"%s" % (_runner.result_line() if problems.is_empty() else ", ".join(problems)))
	if problems.is_empty():
		_lines.append("      config %s, distance %.1f m, optimal %s" % [
			result["config_hash"], float(result["distance_m"]),
			SessionRunner.format_time(float(result["optimal_lap_s"])),
		])


# --- reporting ------------------------------------------------------------------


## The arc-length error between two distances, the short way round a closed lap.
##
## Wrapped, because the last sample of the lap sits at the length and the projection
## hands back zero for it — `TrackLayout.project()` normalizes into `[0, length())`
## on purpose, and an unwrapped comparison would report that one sample as being a
## whole lap out.
func _wrapped_error(got: float, want: float) -> float:
	var total := _layout.length()
	var raw := got - want
	return minf(absf(raw), minf(absf(raw + total), absf(raw - total)))


## One result line. A passing check prints its measurement too — `tuning_probe.gd`'s
## discipline, and the reason it gives: a gate whose output is a column of identical
## words tells a reader nothing about what changed between two runs of it.
func _ok(check_name: String, condition: bool, detail: String = "") -> bool:
	if condition:
		_passed += 1
		_lines.append("check %-34s PASS%s" % [
			check_name, "" if detail.is_empty() else "   " + detail,
		])
	else:
		_failed += 1
		_lines.append("check %-34s FAIL   %s" % [check_name, detail])
	return condition


func _report() -> void:
	_lines.append("")
	_lines.append("checks %d passed %d failed" % [_passed, _failed])
	print("\n".join(_lines))
	quit(1 if _failed > 0 else 0)
