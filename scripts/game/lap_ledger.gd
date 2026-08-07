class_name LapLedger
extends Node

## The per-lap history of one session, because nothing else keeps one.
##
## `circuit.gd` parents this as a node named `LapLedger` and `_ledger_rows()` calls
## `rows()` on it when the session ends. The rows travel to the results sheet
## through `SessionRequest.deliver()`.
##
## ## Why this file has to exist at all
##
## `KartLapTimer` publishes `last_*` and `best_*` and **no history**. There is no
## `lap_at(i)`, no array of `LapRecord`, nothing — `src/session/kart_session.h`'s
## timer section is `has_last / last_time / last_sectors / last_reason /
## last_was_valid` plus the best-lap equivalents, and each one is overwritten by
## the next crossing. A classification sheet needs twelve rows. So somebody has to
## stand next to the timer and write each lap down as it goes past, and this is
## that somebody.
##
## ## The timing fact this file is built on, and it is the whole design
##
## `SessionRunner._tick_running()` does:
##
##     var crossed := _timer.advance(distance, counts_against_the_lap)
##     if crossed:
##         _on_line_crossed()          # emits lap_completed(...)
##
## `advance()` has already closed the lap by the time the signal is emitted, so
## **inside the handler `timer().last_sectors()` is the lap that just ended**.
## One tick later it is still that lap — until the *next* crossing, when it
## silently becomes the following lap's. Reading it from anywhere other than the
## handler is a sheet whose sector columns are off by one lap, which looks
## completely plausible and is wrong on every row. `_on_lap_completed()` is
## therefore the only place in this file that touches the timer.
##
## The signal carries `time_s`, `valid` and `reason` directly, so those three do
## not have that hazard; the sectors are the reason the handler exists.
##
## ## The speed column is a speed trap, not a lap maximum
##
## `speed_kmh` is **one station, one sample**: the kart's ground speed the moment
## it crosses the middle of the circuit's longest straight. That is what the `Spd`
## column on a real timing sheet is — a loop buried in the road at one place — and
## it is emphatically not the fastest the kart went during the lap.
##
## The distinction is not pedantry. A lap maximum is always at least the trap
## reading and usually a few km/h above it, it moves around the circuit from lap
## to lap, and it rewards a driver for a moment of downhill on the exit of
## somewhere else entirely. Two drivers comparing "my Spd was 133" would be
## comparing two different measurements. A column that wears the sheet's label has
## to hold the sheet's number.
##
## The station is `measurements()["longest_straight_at"] + longest_straight / 2`,
## wrapped into the lap. Mid-straight rather than at its end because the end of a
## straight is a braking zone and the reading would then depend on how late the
## driver braked rather than on how fast the kart is.
##
## **No course, no column.** The trap needs a `KartTrack` to know where the
## straight is and a `KartBody` to read the speed off; `configure()` takes both
## rather than reaching for a global, and without either the column reports
## `SPEED_NONE` and the sheet draws a dash. A zero there would read as a stopped
## kart, and a lap maximum quietly substituted would be the wrong measurement
## wearing the right label.

## The `speed_kmh` sentinel for "not measured". Negative, and for the same reason
## `lap_timing.h` gives for `LAP_TIME_NONE` being negative: zero is a plausible
## reading and this is not one. The sheet draws a dash.
const SPEED_NONE := -1.0

## Anything further than this along the lap in a single tick is a teleport — a
## respawn, or the initial projection — and not travel, so no trap crossing is
## inferred across it. Half the lap, which is the same "short way round" rule
## `SessionRunner._advance_odometer()` applies to the odometer: past half a lap
## the sign of the step is ambiguous anyway.
const TELEPORT_FRACTION := 0.5

var _rows: Array = []

var _runner: SessionRunner
var _kart: Object
var _course: Object

## Meters along the lap, or negative when there is no trap. Computed once in
## `configure()` because it is a property of the circuit, not of the lap.
var _trap_station := -1.0
var _length := 0.0

## The reading armed for the lap currently being driven, handed over and cleared
## by `_on_lap_completed()`. One value, not a list: the trap is crossed once per
## lap, and a second crossing before the line means the kart went round again
## without the timer seeing it, which is a bug elsewhere and not something to
## average away.
var _armed_speed := SPEED_NONE

var _last_station := -1.0
var _notes := PackedStringArray()


## Attach to a running session.
##
## `runner` is required. `course` and `kart` are what the speed trap needs and are
## independently optional: a ledger with neither still files every lap, it just
## has no `Spd` column, which is the honest outcome rather than a zero.
##
## Typed `Object` rather than `KartTrack`/`KartBody` for the same reason
## `SessionRunner.configure()` takes a duck-typed course: `TrackLayout` is a
## `Node3D` script and `KartTrack` is a GDExtension `RefCounted`, so no base class
## can name both. The two methods actually used are checked by name below.
func configure(runner: SessionRunner, course: Object = null, kart: Object = null) -> bool:
	if runner == null:
		push_error("LapLedger: configure() needs a SessionRunner")
		return false
	if _runner != null:
		push_error("LapLedger: already configured; one ledger per session")
		return false

	_runner = runner
	_runner.lap_completed.connect(_on_lap_completed)

	_course = course
	_kart = kart
	_arm_trap()
	return true


## Where the speed trap sits, and why it might not sit anywhere.
##
## Every refusal is recorded in `notes()` rather than pushed as a warning: a
## proving-ground session has no `KartTrack` at all and that is a normal way to
## run this game, not a defect worth a red line in the log.
func _arm_trap() -> void:
	if _course == null:
		_notes.append("no course: the speed trap is off and Spd reports a dash")
		return
	if not _course.has_method("length"):
		_notes.append("the course does not publish length(): the speed trap is off")
		return
	_length = float(_course.length())
	if _length <= 0.0:
		_notes.append("the course is %.3f m long: the speed trap is off" % _length)
		return
	if not _course.has_method("measurements"):
		# `TrackRibbon` (the test track) builds its geometry in code and publishes no
		# measurements. Named rather than silent, because "why is there no Spd column
		# on the test track" is otherwise a five-minute question.
		_notes.append("the course publishes no measurements(): the speed trap is off")
		return

	var measured: Dictionary = _course.measurements()
	if not measured.has("longest_straight") or not measured.has("longest_straight_at"):
		_notes.append("measurements() carries no longest_straight: the speed trap is off")
		return
	var straight := float(measured["longest_straight"])
	var at := float(measured["longest_straight_at"])
	if straight <= 0.0:
		_notes.append("the longest straight measures %.3f m: the speed trap is off" % straight)
		return

	# `fposmod` and not `+`: the longest straight can start near the end of the lap
	# and run through the start line, in which case its midpoint is a small station
	# and not one longer than the circuit.
	_trap_station = fposmod(at + straight * 0.5, _length)

	if _kart == null:
		_notes.append("no kart: the trap at %.1f m has nothing to read a speed off"
				% _trap_station)
		_trap_station = -1.0
		return
	if not _kart.has_method("get_speed_ms"):
		_notes.append("the kart does not publish get_speed_ms(): the speed trap is off")
		_trap_station = -1.0
		return
	_notes.append("speed trap at %.1f m, the middle of the %.1f m straight that starts at %.1f m"
			% [_trap_station, straight, at])


# --- the trap ------------------------------------------------------------------


## Watch for the kart crossing the trap.
##
## The station comes from `SessionRunner.distance_m()` — the arc length the timer
## itself last saw — rather than from a second `project()` call. Two reasons, and
## the first is the important one: a second projection is a second opinion, and a
## trap that fired at a station the timer never visited would be measuring a
## slightly different circuit. The second is that `project()` is not free and this
## runs every physics tick.
##
## **Process order against the runner is a real ambiguity of exactly one tick**,
## and it is written down rather than engineered around. If this node is a sibling
## added after `SessionRunner`, its `_physics_process` runs after the runner's, so
## on the tick that crosses the start line the lap has already been filed before
## the trap is examined — a trap reading taken on that same tick would land on the
## next lap. At 60 Hz and 30 m/s that window is 0.5 m of road, and it only bites
## if the trap station falls within it of the start line.
##
## **Valdirone Nuova gets closer to that than is comfortable, so here is the
## measurement rather than a reassurance.** Its longest straight is 165.0 m
## starting at 1,298.1 m on a 1,375.1 m lap — it runs *through* the start line —
## so the trap lands at **5.5 m**, which is 11 ticks past the line at 30 m/s. The
## reading is unambiguously the new lap's, which is correct, because 5.5 m into
## the lap is where the trap is. But the margin is eleven ticks and not a lap, and
## a circuit whose main straight is centered a little differently would sit inside
## it. Any new circuit should have this number checked, which is why
## `trap_station_m()` is public.
func _physics_process(_delta: float) -> void:
	if _trap_station < 0.0 or _runner == null or not _runner.is_running():
		return

	var station := _runner.distance_m()
	if station < 0.0:
		# The runner has not projected yet. `distance_m()` documents the negative.
		return
	var previous := _last_station
	_last_station = station
	if previous < 0.0:
		return

	# Forward travel this tick, the short way round the lap.
	var step := fposmod(station - previous, _length)
	if step <= 0.0 or step > _length * TELEPORT_FRACTION:
		return
	# Distance from where we were to the trap, the same way round. `> 0.0` and not
	# `>= 0.0`: sitting exactly on the trap means it was crossed on a previous tick
	# and counting it again would re-arm with a slower reading on the way out.
	var to_trap := fposmod(_trap_station - previous, _length)
	if to_trap > 0.0 and to_trap <= step:
		_armed_speed = KartCore.ms_to_kmh(float(_kart.get_speed_ms()))


# --- the row -------------------------------------------------------------------


## One lap, filed the instant it closes.
##
## **`last_sectors()` is read here and nowhere else.** See the header: this is the
## only moment it holds the lap named in the arguments.
func _on_lap_completed(number: int, time_s: float, valid: bool, reason: String) -> void:
	var sectors := PackedFloat64Array()
	var timer := _runner.timer()
	if timer != null:
		# Duplicated out of the timer's return so a later `advance()` cannot be seen
		# through it. The C++ side builds a fresh array per call today; that is an
		# implementation detail and a stored row is not the place to depend on one.
		sectors = timer.last_sectors().duplicate()

	_rows.append({
		"lap": number,
		"time_s": time_s,
		"sectors": sectors,
		"speed_kmh": _armed_speed,
		"valid": valid,
		"reason": reason,
	})
	# Disarmed rather than carried forward. A lap with no trap reading — an out lap
	# that started past the trap, a lap driven backwards after a respawn — reports a
	# dash, and it must not inherit the previous lap's number.
	_armed_speed = SPEED_NONE


# --- what came out ---------------------------------------------------------------


## Every lap, in the order they were driven.
##
## A deep copy. `circuit.gd` hands this straight to `SessionRequest.deliver()`,
## which duplicates it again, and the results screen then holds it while this node
## is freed with the track scene — a shared `PackedFloat64Array` across that
## boundary is a reference the sheet would be reading after its owner is gone.
func rows() -> Array:
	return _rows.duplicate(true)


func lap_count() -> int:
	return _rows.size()


## Where the trap ended up, meters along the lap, or negative for no trap. For a
## probe and for the report.
func trap_station_m() -> float:
	return _trap_station


## What this ledger decided about the trap, as lines. `circuit.gd` prints its own
## session report; a ledger that quietly has no `Spd` column has to be able to say
## why when somebody asks.
func notes() -> PackedStringArray:
	return _notes
