class_name SessionRunner
extends Node

## Runs a session. ROADMAP M3c, `docs/GAMEDESIGN.md` §2, ADR-0043.
##
##     var runner := SessionRunner.new()
##     add_child(runner)
##     runner.configure(session, kart, driver, layout)
##     runner.begin()
##
## `GAMEDESIGN.md` §2: *"A session is the only thing the game actually runs.
## Everything above it is data and a results ledger."* Practice, Qualifying, Heat,
## Super Heat and Final are five configurations of this node and not five code
## paths, and the shell hands it a `KartSession` and reads a Dictionary back — which
## is the whole of §9's *"the shell owns no simulation state"*.
##
## ## The six states, and why none of them is a grid
##
## `ROADMAP.md` M6 originally read *"race states: grid, countdown, racing,
## finished, results"*. `GAMEDESIGN.md` §2 rejects that list in advance and says
## why: written that way it absorbs one mode's assumptions — there is a field,
## there is a grid, a lap can be invalidated — and a championship becomes a
## retrofit. This is the list that replaces it:
##
##     SETUP     nothing is running. The configuration has not been checked.
##     HELD      the kart is placed and settling. The driver has no input.
##     GREEN     driving. The clock runs.
##     FLAGGED   the limit has been met. Still driving, still on the clock,
##               ending at the next start-line crossing.
##     RESULT    terminal. A result exists and says what it produced.
##     REFUSED   terminal. The configuration cannot be run, and it names why.
##
## **Not one of those six mentions a field, a grid, a countdown or a position.**
## They are named after the only two facts every session has, whatever its type:
## whether the driver holds the input, and whether the result is final. That is the
## test of whether this is a session runner or a race state machine wearing one's
## name, and it is why Practice is not a special case here — Practice visits
## `SETUP -> HELD -> GREEN -> RESULT` and a Final visits
## `SETUP -> HELD -> GREEN -> FLAGGED -> RESULT`, with the *same* transition table
## and exactly two values different: what releases the hold, and what ends it.
##
## The two racing concepts land as data rather than as states, which is the point:
##
##   * **A grid is an order, not a state.** `race_rules.h`'s `heat_grid()` and
##     `final_grid()` already produce one, before the session starts, from the
##     previous session's classification. The runner is told where to put the kart;
##     it does not compute where that is. ADR-0043 drew that line and this keeps it.
##   * **A countdown is a hold duration, not a state.** `HELD` exists for a reason
##     every session shares and Practice needs most — see `_tick_held` — and a
##     standing start is the same state with a longer minimum.
##
## ## HELD exists for a physical reason, not a ceremonial one
##
## This is the state a naive reading would call "grid" and delete for Practice, and
## deleting it would be a bug. `tools/verify/drive_probe.gd` measured why:
## *"the kart is spawned above the ground and dropped, so the first fraction of a
## second is a landing rather than a measurement"* — `SETTLE_TICKS = 90`, and
## `test_track.gd`'s `SPAWN_LIFT` is 120 mm of deliberate drop, because placing a
## kart at exactly wheel height starts the first tick with the tires already
## interpenetrating.
##
## So every session, including an open Practice session, has a moment where the
## kart is on the track and is not yet a kart anyone should be driving or timing. A
## driver given the throttle mid-drop gets a launch off a bounce; a lap timer armed
## mid-drop arms from a position 120 mm above where the kart ends up. `HELD` is that
## moment, its release condition is the physical fact that the kart has landed, and
## a race's countdown is a *floor* added to the same wait.
##
## ## What it applies to the kart, and what a restorer applies itself
##
## `SessionConfig` carries the assists and the tuning set. The assists are applied
## here, because they change the lap and are in the configuration hash for that
## reason. The tuning is **not** applied here, and since issue #178 that is a
## choice rather than a missing method: `KartSession.apply_tuning(KartTuning)`
## pushes a configuration's tuning onto the registry, and it belongs to whoever
## *restored* the session — a replay player, a save loader — not to this node. A
## session started from a scene records what the scene was already tuned to
## (`adopt_tuning`, the other direction), and a runner that re-imposed it on
## every configure() would fight the F2 overlay for the same knobs mid-session.
##
## ## Input is gated at the producer, and this node is its only owner
##
## ADR-0040: `KartBody` is handed its input and never reaches for it, so
## `PlayerDriver.enabled` is where a pause, a menu or a countdown gets its
## authority. **Two owners of that flag is a bug waiting to happen**, and there was
## nearly one: `test_track.gd`'s free camera sets `_driver.enabled = false` so WASD
## can fly the view, and a runner that also wrote the flag would hand the throttle
## back to a driver who is flying a camera the moment a state changed. So the flag
## has exactly one writer — `_apply_input_gate()` — and everything else that wants
## the input off calls `set_input_suspended()`. The driver is enabled when the state
## allows it *and* nothing has suspended it.
##
## ## Where the persistence join goes
##
## Nothing here saves and nothing here loads, deliberately: the profile and the
## ghost are another agent's files. What this node produces is `result()`, a
## Dictionary carrying the best lap, its sectors and the configuration hash it was
## set under, plus the `session_finished` signal that hands the same Dictionary
## over. **A profile writer subscribes to `session_finished` and stores
## `result()["best_lap_s"]` against `result()["config_hash"]`; a ghost recorder
## subscribes to `lap_completed` and keeps the stream of the lap that was fastest.**
## Those are the two seams and they are the only two.
##
## Since issue #178 the result also carries `classification()`, a bound
## `KartClassification` holding the same measurements as typed `DriverResult`
## fields — because the Dictionary's spelling of those keys was an agreement,
## and `score_round()` will want eight of these the day a Heat has a field. The
## player is filed as **driver id 0**: ids are session-local integers
## (`kart_session.h` says why they are not roster slugs), and ADR-0047's draw
## will own the mapping when a field exists.

# --- states --------------------------------------------------------------------

enum {
	STATE_SETUP,
	STATE_HELD,
	STATE_GREEN,
	STATE_FLAGGED,
	STATE_RESULT,
	STATE_REFUSED,
	STATE_COUNT,
}

## The names, for a report line and for the HUD. Lower case and stable, because
## they are compared in `tools/verify/session_probe.gd` and printed by the scene.
const STATE_NAMES: PackedStringArray = [
	"setup", "held", "green", "flagged", "result", "refused",
]

## The transition table, as the set of states each state may move to.
##
## Written out rather than left to the `if`s that implement it, because the probe
## asserts against it: a state machine whose legal moves exist only as scattered
## conditions cannot be checked for the two failures that matter — a state entered
## twice, and a state skipped. `_enter()` refuses anything not in this table and
## says so, so an illegal transition is an error at the moment it is attempted
## rather than a session that quietly ran a phase it should not have.
##
## `SETUP -> RESULT` is deliberately absent. A session that never started produced
## nothing, and letting it hand back a result would make "there is a result" stop
## meaning "a kart drove".
const TRANSITIONS: Dictionary = {
	STATE_SETUP: [STATE_HELD, STATE_REFUSED],
	STATE_HELD: [STATE_GREEN, STATE_RESULT],
	STATE_GREEN: [STATE_FLAGGED, STATE_RESULT],
	STATE_FLAGGED: [STATE_RESULT],
	STATE_RESULT: [],
	STATE_REFUSED: [],
}

# --- timing --------------------------------------------------------------------

## Sectors, before anybody has authored splits.
##
## Three, which `lap_timing.h` calls *"the convention every timing screen in the
## sport uses"*. They are placed by `KartLapTimer.begin_even`, and that is a
## **placeholder that says so by being one line**: ADR-0046 puts real splits in
## `track.json` and M5 writes that schema. When it does, this becomes
## `begin_marks(track.sector_marks, length, step)` and nothing else here moves.
const SECTOR_COUNT := 3

## How long `HELD` will wait for the kart to land before releasing it anyway.
##
## `drive_probe.gd` measured the landing at `SETTLE_TICKS = 90` — 0.75 s at 120 Hz
## — for a kart dropped from `test_track.gd`'s 120 mm `SPAWN_LIFT`. 240 ticks is 2 s
## and 2.7 times that, so a kart that has not settled by here is not settling: it
## has been placed inside geometry, over a hole, or on its side. Releasing anyway
## and saying so is better than holding forever, because a session that never starts
## and never explains itself is indistinguishable from a hung game.
const HOLD_CEILING_TICKS := 240

## Below this speed the kart counts as standing still, m/s. 0.5 m/s is 1.8 km/h,
## which is slower than the kart creeps at idle in first with the clutch in, so it
## cannot be reached by a kart that is being driven and can only mean a kart at rest.
const SETTLED_SPEED_MS := 0.5

## Multiplier on the kart's own top speed above which a step in arc length is a
## discontinuity rather than travel, for the odometer.
##
## The figure it multiplies comes from `KartCore.kz_reference()` rather than from a
## literal here, so this file holds no copy of a §6.4 number. Two, for the same
## reason `lap_timing.h` sets `LAP_MAX_SPEED_MS` half again above the top speed: no
## amount of downhill or curb launch reaches it, and anything past it is a teleport,
## a respawn, or a scene that moved the body without saying so — none of which is
## distance the kart covered.
const ODOMETER_STEP_LIMIT_FACTOR := 2.0

# --- configuration -------------------------------------------------------------

var _session: KartSession
var _kart: KartBody
var _driver: PlayerDriver
var _layout: TrackLayout
var _timer: KartLapTimer

var _state := STATE_SETUP
var _refusal := ""

## Extra ticks `HELD` waits after the kart has landed.
##
## Zero, and it stays zero until a start procedure is sourced. Practice and
## Qualifying release the driver from the pits when they like, so zero is *correct*
## for them rather than a placeholder. A standing start's countdown is a real
## duration in the FIA General Prescriptions and `docs/REFERENCES.md` does not have
## it, so this is a setter and not a constant: inventing three seconds here and
## dressing it as a rule is the failure `race_rules.h`'s header spends thirty lines
## on. Whoever sources it sets it; nobody guesses it.
var _hold_ticks := 0

## Whether something outside the state machine wants the input off — the free
## camera, a pause, an overlay. See the header on why this is not a second writer
## of `PlayerDriver.enabled`.
var _input_suspended := false

# --- run state -----------------------------------------------------------------

var _held_ticks := 0
var _session_ticks := 0
var _flag_tick := -1

## Last tick's arc length, which is the hint that tells the two ends of the hairpin
## apart. Negative means "no hint" and forces a global projection: the first tick of
## a session, and after a respawn has moved the kart somewhere the hint does not
## describe. `track_layout.gd`'s `project()` header has the argument.
var _hint := -1.0

## Arc length the timer was armed at. Reported so a session's own out lap is
## measurable rather than assumed — see `_limit_met`.
var _arm_distance := 0.0

## Distance covered along the track, meters. `DriverResult.distance_m`'s figure, and
## `race_rules.h` is explicit that it is measured rather than derived from laps: a
## driver who retires halfway round a lap has covered that half, and the
## part-distance credit is a distance test.
var _odometer := 0.0

var _odometer_step_limit := 0.0

## Filled once, when the session reaches `RESULT` or `REFUSED`. Held rather than
## recomputed so that reading a result twice cannot give two answers.
var _result: Dictionary = {}

## The same finish, as the type `race_rules.h` scores. Empty (count 0) for a
## refused session — a session that never ran classified nobody.
var _classification: KartClassification

## What ended the session, in the vocabulary `result()["outcome"]` publishes.
var _outcome := ""

signal state_changed(from: int, to: int)

## A lap crossed the line. `valid` is whether it counted and `reason` is
## `lap_timing.h`'s name for why it did not. **This is the ghost recorder's seam**
## — see the header.
signal lap_completed(number: int, time_s: float, valid: bool, reason: String)

## The session is over and `result()` is final. **This is the profile writer's
## seam.** The Dictionary is passed as well as being readable, so a subscriber does
## not have to hold the runner.
signal session_finished(result: Dictionary)


# --- setting up ----------------------------------------------------------------


## Hand the runner everything it needs. Returns false and refuses the session when
## the configuration cannot be run.
##
## Every argument is required and none of them is looked up by name or by group. A
## runner that found its own kart would be a second place that decides which kart a
## session is about, and eight karts in one scene is on the roadmap.
func configure(
	session: KartSession, kart: KartBody, driver: PlayerDriver, layout: TrackLayout
) -> bool:
	if _state != STATE_SETUP:
		push_error("SessionRunner: configure() after the session started, in state %s"
			% state_name())
		return false

	_session = session
	_kart = kart
	_driver = driver
	_layout = layout

	var problem := _first_problem()
	if problem != "":
		_refusal = problem
		_enter(STATE_REFUSED)
		return false

	_timer = KartLapTimer.new()
	var step := 1.0 / float(Engine.physics_ticks_per_second)
	if not _timer.begin_even(_layout.length(), SECTOR_COUNT, step):
		# `begin_even` has already said what was wrong with the numbers. What this
		# adds is that the session is refused rather than run with a timer that
		# answers `advance()` with an error every tick — which `KartLapTimer::advance`
		# warns about once and then stays quiet about forever.
		_refusal = "the lap cannot be timed: %.3f m in %d sectors" % [
			_layout.length(), SECTOR_COUNT,
		]
		_enter(STATE_REFUSED)
		return false

	# The assists, which are configuration and are hashed as such. `session.h`: they
	# are in the fingerprint *because* they change the lap — a replay recorded with
	# auto-shift on re-sims into a different gear on every corner without it.
	_kart.auto_clutch = _session.is_auto_clutch()
	_kart.auto_shift = _session.is_auto_shift()

	_odometer_step_limit = ODOMETER_STEP_LIMIT_FACTOR \
		* KartCore.kmh_to_ms(float(KartCore.kz_reference()["top_speed_max_kmh"])) * step

	# The input goes off now rather than at the first tick. `configure()` is called
	# from a scene's `_ready`, and between there and the first `_physics_process`
	# there is at least one rendered frame in which a driver holding the throttle
	# would be driving a kart that has not been placed.
	_apply_input_gate()
	return true


## What is wrong with this configuration, as a sentence, or an empty String.
##
## The two refusals here are **"this is not built"**, named as such, and that is
## `GAMEDESIGN.md` §13's rule rather than a limitation being hidden: *"a stubbed
## mode reads worse than an absent one, because it advertises an intention the code
## does not honor"*. A Heat that ran with one kart and called it a Heat would be
## exactly that.
##
## Note which refusals are **not** here. Nothing is refused by session *type*.
## A Heat with an entry list of one is a legal configuration and it runs — a
## distance limit, a standing start, lap invalidation off — because refusing by type
## is the coupling this whole file exists to avoid. What is refused is the two things
## that genuinely do not exist yet, and both name themselves.
func _first_problem() -> String:
	if _session == null or _kart == null or _driver == null or _layout == null:
		return "configure() wants a session, a kart, a driver and a layout"
	if not _session.is_valid():
		# `SessionConfig::is_valid()` is total and cheap and `KartSession` exposes it
		# precisely so both a menu and the runner can call it. `session.h` says so.
		return "the session configuration is not valid — see KartSession.is_valid()"
	if _session.get_kart_class() != KartSession.CLASS_KZ2:
		# `session.h`: *"Nothing in `src/` models OK yet ... Anything that has to
		# simulate a class asserts on this rather than assuming."* This is that
		# assertion. A session run as OK against the KZ2 solver would produce a lap
		# time filed under a class nobody simulated.
		return "class %s is not simulated — nothing in src/ models it" % (
			KartSession.kart_class_name(_session.get_kart_class())
		)
	if _session.get_tick_hz() != Engine.physics_ticks_per_second:
		# Issue #174: the rate is in the configuration hash because two sessions
		# differing only in integration rate are not the same session. A config
		# claiming a rate the engine will not run at would record a replay whose
		# header lies about its own integration, so it is refused here, at the
		# same boundary that refuses a class nobody simulates.
		return "the session claims %d Hz and the engine runs %d" % [
			_session.get_tick_hz(), Engine.physics_ticks_per_second,
		]
	if _session.get_entry_count() > 1:
		# There is no field. ADR-0047's roster, the AI of M7 and `GAMEDESIGN.md` §6's
		# voice culling are all prerequisites, and §13.B says so in as many words:
		# *"a grid of karts the AI cannot race is a worse artifact than a time trial
		# with no grid at all."*
		return "an entry list of %d needs a field, and there is none yet" % (
			_session.get_entry_count()
		)
	return ""


## Extra ticks to hold after the kart has landed. See `_hold_ticks`.
func set_hold_ticks(ticks: int) -> void:
	_hold_ticks = maxi(0, ticks)


## Turn the driver's input off without changing the session's state.
##
## The free camera's call, and eventually the pause menu's. Additive with the state
## gate rather than an override of it: suspending during `HELD` and un-suspending
## does not hand the input to a driver whose kart is still falling.
func set_input_suspended(suspended: bool) -> void:
	if _input_suspended == suspended:
		return
	_input_suspended = suspended
	_apply_input_gate()


func is_input_suspended() -> bool:
	return _input_suspended


# --- running -------------------------------------------------------------------


## Place the kart and start holding it. `SETUP -> HELD`.
func begin() -> bool:
	if _state != STATE_SETUP:
		push_error("SessionRunner: begin() in state %s" % state_name())
		return false
	if _timer == null:
		push_error("SessionRunner: begin() before a successful configure()")
		return false
	_enter(STATE_HELD)
	return true


## End the session now, whatever state it is in.
##
## **This is what "Practice ends when you leave" is**, and it is the only way an
## `Open` session can finish: `GAMEDESIGN.md` §4 gives Practice no limit at all, so
## nothing inside the simulation can end it and the decision belongs to whoever
## opened it. The shell's pause menu calls this; `tools/verify/session_probe.gd`
## calls it; `test_track.gd` deliberately does not bind it to a key, because there is
## no pause screen to confirm it against and a key that silently ends a session is
## worse than one that does not exist.
##
## `outcome` is recorded verbatim in the result, so a caller says why in its own
## words and the result can be read back without guessing.
func end_session(outcome := "abandoned") -> bool:
	if _state == STATE_RESULT or _state == STATE_REFUSED:
		return false
	if _state == STATE_SETUP:
		# `SETUP -> RESULT` is not in the transition table and the reason is in its
		# comment: a session that never started produced nothing. Refused instead, so
		# that the caller gets a terminal state and a sentence rather than a result
		# describing a kart that never moved.
		_refusal = "end_session(%s) before begin()" % outcome
		_enter(STATE_REFUSED)
		return false
	_outcome = outcome
	_enter(STATE_RESULT)
	return true


## The kart was put back on the track by something other than the driver.
##
## Called by whoever owns the pose — `test_track.gd` does, and its own comment says
## why: *"`respawn()` moves a body to a pose only whoever placed it knows"*. The
## runner is told rather than doing it, and what it does with the news is two things
## the scene cannot: kill the lap, and throw away the projection hint.
##
## **Throwing away the hint is the half that would be invisible.** A respawn moves
## the kart hundreds of meters in one tick, and a windowed projection around a stale
## hint would return the nearest point of a window the kart has left, confidently
## and wrongly — see `TrackLayout.project()`. The next tick projects globally.
func notify_respawn() -> void:
	# Ignored outside a running session, and not because it is harmless: a respawn
	# during `HELD` would arm the timer's marks from the respawn pose, and `_release`
	# would then advance from the settled pose without re-arming — a session armed
	# from a position the kart was never at.
	if not is_running():
		return
	# Globally, with no hint, because the hint is exactly what has just been
	# invalidated.
	var placed := _layout.project(_kart.global_position, -1.0)
	_hint = placed["distance"]
	_timer.respawn(_hint)
	# The lap dies here and the session does not, in Practice.
	#
	# `GAMEDESIGN.md` §4 takes a real regulation — *"a kart restarted with outside
	# help is disqualified from that session"* — and `lap_timing.h` records the
	# respawn for it while saying the session-level decision is this file's. It is
	# taken as: the lap always dies, and the session survives in Practice only.
	#
	# The argument is that a Practice disqualification has nothing to express.
	# `race_rules.h`'s own predicates say so: Practice neither
	# `session_awards_championship_points` nor `session_sets_next_grid`, so there is
	# no classification for it to be struck from. Every other type feeds something,
	# and a driver who was pushed back onto the track has not driven that session.
	#
	# **Both predicates are in `race_rules.h` and neither is bound to GDScript**, so
	# the type test below is a second reader of a fact that header owns. That is a
	# wart and it is reported rather than papered over.
	if _session.get_type() != KartSession.TYPE_PRACTICE:
		_outcome = "disqualified: restarted with outside help"
		_enter(STATE_RESULT)


func _physics_process(_delta: float) -> void:
	match _state:
		STATE_HELD:
			_tick_held()
		STATE_GREEN, STATE_FLAGGED:
			_tick_running()


## Wait for the kart to land. See the header for why this is not ceremony.
##
## Two release conditions and a ceiling. The kart has to be on all four wheels *and*
## stopped, because a kart mid-bounce touches down with four wheels for one tick on
## its way back up, and arming the timer on that tick arms it from a position 100 mm
## above the road. The ceiling releases anyway and says so.
func _tick_held() -> void:
	_held_ticks += 1
	var landed := _kart.wheels_on_ground >= 4 and _kart.speed_ms < SETTLED_SPEED_MS
	if landed and _held_ticks >= _hold_ticks:
		_release()
		return
	if _held_ticks >= HOLD_CEILING_TICKS:
		push_warning(
			"SessionRunner: released after %d ticks with %d of 4 wheels down at %.2f m/s — "
			% [_held_ticks, _kart.wheels_on_ground, _kart.speed_ms]
			+ "the kart has not settled, so the lap is armed from wherever it is"
		)
		_release()


## Arm the timer from where the kart actually came to rest and hand over the input.
func _release() -> void:
	var placed := _layout.project(_kart.global_position, -1.0)
	_hint = placed["distance"]
	_arm_distance = _hint
	# The first `advance()` is what arms the marks — `LapTimer::arm_marks_from` — so
	# it happens here, from the settled position, rather than at some point during the
	# drop. `off_track` is measured rather than assumed false: a session can legally
	# be set up with the kart on the grass.
	_timer.advance(_hint, _measure_off_track(placed))
	_enter(STATE_GREEN)


func _tick_running() -> void:
	_session_ticks += 1

	var placed := _layout.project(_kart.global_position, _hint)
	var distance: float = placed["distance"]
	_advance_odometer(distance)
	_hint = distance

	var off_track := _measure_off_track(placed)
	# **The invalidation rule is the session type's, and it is ours rather than the
	# FIA's.** `lap_timing.h` says both halves: the off-track *definition* is General
	# Prescriptions Art. 2.14 B, and the penalty for going off by yourself is this
	# project's invention because the regulations attach none. So a race is told the
	# kart is on the road even when it is not — there is nothing in a race scored per
	# lap for it to cost, and invalidating a race lap would be inventing a rule the
	# sport does not have *and* one with no effect.
	var counts_against_the_lap := off_track \
		and KartLapTimer.type_invalidates_laps(_session.get_type())
	var crossed := _timer.advance(distance, counts_against_the_lap)

	if crossed:
		_on_line_crossed()
	if _state == STATE_RESULT:
		return

	if _state == STATE_GREEN and _limit_met():
		_flag_tick = _session_ticks
		_enter(STATE_FLAGGED)


## One start-line crossing.
##
## The order matters: the lap is published, then the flag is tested. Testing the flag
## first would end a `Laps` session one lap early, and it would publish a result
## before the lap that produced it.
func _on_line_crossed() -> void:
	lap_completed.emit(
		_timer.laps_completed(), _timer.last_time(), _timer.last_was_valid(),
		_timer.last_reason()
	)

	# **The one rule that ends every limited session: the flag falls when the limit
	# is met, and the session ends at the first line crossing at or after it.**
	#
	# That is what a chequered flag is, and writing it as one rule rather than three
	# is what makes a `Laps` limit fall out for free: the crossing that met the lap
	# count is itself "at or after" the flag, so a five-lap session ends on the fifth
	# crossing and not the sixth. A distance or a duration is met mid-lap, so the
	# driver finishes the lap they are on. An `Open` session never flags, which is why
	# `end_session()` is the only thing that can end Practice.
	if _state == STATE_FLAGGED:
		_outcome = "finished"
		_enter(STATE_RESULT)
	elif _limit_met():
		_flag_tick = _session_ticks
		_outcome = "finished"
		_enter(STATE_FLAGGED)
		_enter(STATE_RESULT)


## Has the session's limit been reached.
##
## `session.h`'s four kinds, and the arithmetic is one line each. Time is counted in
## **ticks**, never from a clock: `lap_timing.h` opens with the reason and it is not
## a style preference — `max_physics_steps_per_frame` clamps without banking, so
## under a stall simulation time falls behind wall-clock by a measured 0.6476, and a
## session limit read off a wall clock would end a race early on a slow machine.
##
## **A `Laps` limit counts start-line crossings, and that is a statement about the
## grid rather than about lap counting.** `test_track.gd` puts its grid 30 m *past*
## the line, so the first crossing there comes after 1,000 m of a 1,030 m lap and a
## one-lap session is 30 m short. A real starting grid sits behind the line and the
## first crossing is a little over a lap, which is exactly what a real race counts.
## The alternative — discounting the first crossing as an out lap — was written and
## removed: it makes a five-lap standing start take six crossings, which no race
## does, and `lap_timing.h` already stops that lap from becoming a best lap, which is
## the only place the distinction has a consequence.
func _limit_met() -> bool:
	var value := _session.get_limit_value()
	match _session.get_limit_kind():
		KartSession.LIMIT_LAPS:
			return float(_timer.laps_completed()) >= value
		KartSession.LIMIT_DISTANCE:
			return _odometer >= value
		KartSession.LIMIT_DURATION:
			return elapsed_s() >= value
	# `LIMIT_OPEN`. Practice, and nothing inside the simulation ends it.
	return false


## Add this tick's travel to the odometer, ignoring anything too large to be travel.
func _advance_odometer(distance: float) -> void:
	var total := _layout.length()
	var step := distance - _hint
	# The short way round, so the tick that crosses the start line is a small forward
	# step rather than a whole lap backwards. `lap_timing.h`'s `circular_delta` makes
	# the same correction for the same reason, and getting it wrong here would credit
	# a lap of distance for a kart nudging over the line.
	if step > total * 0.5:
		step -= total
	elif step < -total * 0.5:
		step += total
	# Forward only, and inside a plausible step. A kart reversing does not un-drive
	# the distance it covered, and a step larger than the kart can travel in one tick
	# is a teleport rather than travel.
	if step > 0.0 and step <= _odometer_step_limit:
		_odometer += step


## Whether all four wheels are outside the track's edge lines.
##
## **The definition is the FIA's and nothing else.** General Prescriptions
## Art. 2.14 B: *"If the four wheels of a kart are outside these lines, the kart is
## considered as having left the track."* `lap_timing.h` takes a bool meaning exactly
## that and deliberately does not decide it, because counting wheels is the
## boundary's job — this is the boundary.
##
## The lines are where `track_ribbon.gd` paints them, and its comment settles which
## side of them counts: the edge lines are laid **inboard** of the asphalt edge *"so
## that the painted line is the last thing with grip on it and a wheel on the white
## is still a wheel on the track"*. So the line's outer edge is the asphalt edge at
## half the track width, and "outside the lines" is "past half the track width".
##
## Three things this deliberately does not use.
##
## **Not the surface metadata.** A wheel's `surface` would be the obvious test and it
## is the wrong one twice: `--grass=false` makes the run-off asphalt, which would
## mean no lap could ever be invalidated in a scene that is otherwise identical; and
## the curb is *outboard* of the line, so a curb strike is genuinely outside the line
## and a surface test would need the same geometry anyway to know that.
##
## **Not the contact point.** A wheel in the air is somewhere, and the FIA sentence
## is about position rather than contact. `wheel_report()["point"]` is latched when
## a wheel is buried and stale when it is airborne; `origin` is the ray origin from
## the chassis and is always this tick's answer.
##
## **Not a tire width**, because nothing publishes one. This measures the wheel
## *center*, which puts the threshold half a tire outside the true line — more
## generous than the regulation, and erring toward not striking out a lap is the
## right direction for a rule this project admits is its own.
func _measure_off_track(placed: Dictionary) -> bool:
	var half_width := TrackRibbon.TRACK_WIDTH * 0.5
	var right := TrackLayout.right(placed["heading"])
	var centerline_lateral: float = placed["lateral"]
	var origin := _kart.global_position
	# All four wheels are placed in the frame the body was projected into rather than
	# projected one at a time. `TrackLayout.project()`'s header carries the measured
	# cost that decides it — 24.4 us a call, so five calls is 1.5% of every tick — and
	# the error the shortcut costs is bounded by the kart's own length against the
	# corner radius: 0.9 m ahead of the body in the 11 m hairpin is 37 mm of
	# difference, against a 4 m threshold.
	for wheel: Dictionary in _kart.wheel_report():
		var offset: Vector3 = (wheel["origin"] as Vector3) - origin
		if absf(centerline_lateral + offset.dot(right)) <= half_width:
			return false
	return true


# --- reading it back -----------------------------------------------------------


func state() -> int:
	return _state


func state_name() -> String:
	return STATE_NAMES[_state] if _state < STATE_NAMES.size() else "?"


## Why the session was refused, or an empty String. Only ever set in `REFUSED`.
func refusal() -> String:
	return _refusal


## The lap timer, for a HUD. One per kart, which is why it is owned here and not
## globally: `lap_timing.h` says a field is `entry_count` of these.
func timer() -> KartLapTimer:
	return _timer


func is_running() -> bool:
	return _state == STATE_GREEN or _state == STATE_FLAGGED


func is_over() -> bool:
	return _state == STATE_RESULT or _state == STATE_REFUSED


## Whether the chequered flag has fallen. A HUD says so; nothing else needs it.
func is_flagged() -> bool:
	return _state == STATE_FLAGGED


## Seconds the session has run, from ticks. Never from a clock — see `_limit_met`.
func elapsed_s() -> float:
	return float(_session_ticks) / float(Engine.physics_ticks_per_second)


func session_ticks() -> int:
	return _session_ticks


func laps_completed() -> int:
	return _timer.laps_completed() if _timer != null else 0


## Arc length the lap timer was armed at, meters. Zero means the session started on
## the start line; anything else is how far the out lap was short by.
func arm_distance_m() -> float:
	return _arm_distance


func odometer_m() -> float:
	return _odometer


## Arc length along the lap, meters, as the timer last saw it. Negative before the
## first projection.
func distance_m() -> float:
	return _hint


## How much of the limit has been used, 0 to 1, or -1 for a session with no limit.
##
## A HUD's progress figure, computed here because the alternative is the HUD holding
## its own copy of the four limit kinds — and then a fifth kind is a screen that
## silently stops showing progress.
func limit_fraction() -> float:
	var value := _session.get_limit_value() if _session != null else 0.0
	if value <= 0.0:
		return -1.0
	match _session.get_limit_kind():
		KartSession.LIMIT_LAPS:
			return clampf(float(_timer.laps_completed()) / value, 0.0, 1.0)
		KartSession.LIMIT_DISTANCE:
			return clampf(_odometer / value, 0.0, 1.0)
		KartSession.LIMIT_DURATION:
			return clampf(elapsed_s() / value, 0.0, 1.0)
	return -1.0


## What the session produced. Empty until it is over.
##
## **A Dictionary rather than a `Classification`.** `race_rules.h`'s `DriverResult`
## is exactly this shape and is the type this should become, and it is not bound to
## GDScript — `src/register_types.cpp` registers `KartSession` and `KartLapTimer`
## and nothing else from `src/session/`. So the keys below are named after
## `DriverResult`'s fields so the join is mechanical when the bridge exists:
## `laps_completed`, `distance_m` and `best_lap_s` are that struct's three
## measurements under its own names.
func result() -> Dictionary:
	return _result.duplicate()


## The finish as `race_rules.h`'s type, or null before the session is over. The
## same measurements as the Dictionary's `laps_completed` / `distance_m` /
## `best_lap_s` keys, spelled by a struct instead of an agreement — issue #178.
func classification() -> KartClassification:
	return _classification


## The result as one sentence, for a terminal and for a report.
##
## ADR-0044: one format string with placeholders, never a sentence built by
## concatenation, so the unit of text is a whole sentence a translator could reorder.
func result_line() -> String:
	if _state == STATE_REFUSED:
		return "session refused: %s" % _refusal
	if _result.is_empty():
		return "session %s, no result yet" % state_name()
	if not bool(_result["has_best"]):
		return "%s over after %d laps in %s: no timed lap, %s" % [
			_result["type_name"], int(_result["laps_completed"]),
			format_time(float(_result["elapsed_s"])), _result["outcome"],
		]
	return "%s over after %d laps in %s: best %s, %d valid and %d struck out, %s" % [
		_result["type_name"], int(_result["laps_completed"]),
		format_time(float(_result["elapsed_s"])), format_time(float(_result["best_lap_s"])),
		int(_result["valid_laps"]), int(_result["invalid_laps"]), _result["outcome"],
	]


## `m:ss.mmm`, which is how the sport writes a lap time, and a dash when there is
## none.
##
## **Not zero for "no lap".** `lap_timing.h` is emphatic: `LAP_TIME_NONE` is
## negative because *"zero is a plausible-looking lap time and it sorts first, so a
## fresh session's best would beat every real lap forever"*. A formatter that turned
## that into `0:00.000` would put the same lie back on the screen.
##
## Static so the HUD and the report format one way. Truncated rather than rounded, as
## timing screens do: a lap that took 1:23.4569 is shown as 1:23.456, because
## rounding up shows a driver a time they did not do.
static func format_time(seconds: float) -> String:
	if seconds <= 0.0:
		return "-:--.---"
	var milliseconds := int(seconds * 1000.0)
	return "%d:%02d.%03d" % [
		milliseconds / 60000, (milliseconds / 1000) % 60, milliseconds % 1000,
	]


## A delta against a reference, `+0.123` or `-0.123`, negative being ahead.
##
## **Signed always, including at zero.** A bare `0.000` in a column of signed
## numbers reads as missing rather than as level, and level is real news.
static func format_delta(seconds: float) -> String:
	return "%+.3f" % seconds


# --- the state machine ---------------------------------------------------------


## Move to a state, or refuse to.
##
## The check against `TRANSITIONS` is what makes the table above the definition
## rather than documentation, and it catches the two failures the probe asks about:
## a state entered twice — every state's own list excludes itself — and a state
## skipped, because `SETUP -> GREEN` is not in any row.
func _enter(next: int) -> void:
	var allowed: Array = TRANSITIONS.get(_state, [])
	if not allowed.has(next):
		push_error("SessionRunner: %s -> %s is not a legal transition" % [
			state_name(), STATE_NAMES[next] if next < STATE_NAMES.size() else str(next),
		])
		return
	var previous := _state
	_state = next
	_apply_input_gate()
	if next == STATE_RESULT or next == STATE_REFUSED:
		_build_result()
	state_changed.emit(previous, next)
	if next == STATE_RESULT or next == STATE_REFUSED:
		session_finished.emit(result())


## `PlayerDriver.enabled`, from one place. See the header on why there is one writer.
func _apply_input_gate() -> void:
	if _driver == null:
		return
	_driver.enabled = is_running() and not _input_suspended


func _build_result() -> void:
	var type := _session.get_type() if _session != null else KartSession.TYPE_PRACTICE
	if _outcome == "":
		_outcome = "refused" if _state == STATE_REFUSED else "abandoned"

	# The typed half of the result, issue #178. The player is driver id 0 — ids
	# are session-local, per the header — and is classified (position 1, alone in
	# the field) only when a valid lap exists: `race_rules.h` reads position 0 as
	# "not classified", which is the honest entry for a session that produced no
	# timed lap. A refused session classifies nobody at all.
	_classification = KartClassification.new()
	_classification.begin(type)
	if _state == STATE_RESULT and _timer != null:
		var position := 1 if _timer.has_best() else 0
		_classification.add_result(0, position, _timer.laps_completed(), _odometer,
			_timer.best_time() if _timer.has_best() else 0.0)
	_result = {
		"type": type,
		"type_name": KartSession.type_name(type),
		# The configuration this result was set under, so a saved best lap can be
		# compared against the session that produced it rather than against a
		# configuration nobody recorded. ADR-0041's `config_hash`, and the reason the
		# profile join needs no second key.
		"config_hash": _session.config_hash_hex() if _session != null else "",
		"outcome": _outcome,
		"state": _state,
		"refusal": _refusal,
		"ticks": _session_ticks,
		"elapsed_s": elapsed_s(),
		# `DriverResult`'s three measurements, under its own field names.
		"laps_completed": _timer.laps_completed() if _timer != null else 0,
		"distance_m": _odometer,
		"best_lap_s": _timer.best_time() if _timer != null else -1.0,
		# And the rest of what a timing screen and a ghost want.
		"arm_distance_m": _arm_distance,
		"valid_laps": _timer.valid_lap_count() if _timer != null else 0,
		"invalid_laps": _timer.invalid_lap_count() if _timer != null else 0,
		"has_best": _timer.has_best() if _timer != null else false,
		"best_sectors": _timer.best_sectors() if _timer != null else PackedFloat64Array(),
		"optimal_lap_s": _timer.optimal_lap() if _timer != null else -1.0,
		"flagged_at_tick": _flag_tick,
	}
