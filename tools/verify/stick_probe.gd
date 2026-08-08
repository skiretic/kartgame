extends SceneTree

## Issue #239: does the smallest stick deflection at speed reach the front wheels
## as an angle that could spin the kart?
##
## This measures **the stick -> `input.steer` path only**, which is the half of
## #239 that nothing in this repo had ever instrumented. `input_push_probe.gd` is
## the sole other gate that goes through `PlayerDriver` at all, and it presses
## `Input.action_press("steer_left", 1.0)` — a synthesized *action* at an explicit
## strength, which skips the InputMap's joypad-axis deadzone arithmetic entirely.
## Every other probe and every still sets `KartBody.input_driver`, which
## **overrides** the pushed input per ADR-0040. So between the axis and the front
## wheel there were three unmeasured stages: the deadzone law, the curve's runtime
## exponent, and the steering solve.
##
##     godot --headless --path . --script tools/verify/stick_probe.gd \
##       -- --case=deadzone,gamma,lock,depart --scene=circuit
##
##       --case=<a,b>   a subset; the probe prints which ran
##       --break=<mode> a negative control. Exit code is INVERTED under it.
##                        deadzone  zero the InputMap deadzone on both steer
##                                  actions, so the measured law stops being the
##                                  0.15 rescale
##                        gamma     force steer_gamma to 1.0 every tick, which is
##                                  the linear branch of `steering_curve`
##                        dead      PlayerDriver.enabled = false, so nothing the
##                                  stick does reaches the solver
##       --stick=<f>    the raw axis deflection `depart` holds. Default is
##                      SMALL_STICK, one 8-bit code past the deadzone. 0.0 is the
##                      reference run that decides whether the stick did anything.
##       --scene=<s>    `circuit` (valdirone.tscn, the default) or `flat` (the
##                      proving ground). Same rig, different ground.
##       --coast        release the throttle for the hold phase instead of holding
##                      it flat. Separates a ground that yaws the kart from a
##                      power-on instability.
##       --reference=<deg>
##                      the peak body slip this same run reached at stick 0.0.
##                      Turns `depart`'s verdict from "did it spin" into "did the
##                      STICK spin it", which is the only question #239 owns.
##                      `stick.sh` measures it and passes it.
##
## ## The four cases
##
## `deadzone` — what `Input.get_action_strength` actually returns for a joypad
## axis past a 0.15 deadzone. Measured rather than recalled, because
## `player_driver.h` asserted the opposite for a milestone and the exponent's whole
## derivation rested on it. No scene; this is an engine-behavior measurement with
## two analytic models and the check is which one fits.
##
## `gamma` — which branch of `PlayerDriver::steering_curve` runs **in the shipped
## scene at runtime**. `steer_gamma` reaches the node over `KartTuning`'s
## `tuning_changed` signal, because the registry only knows how to reach a
## `KartBody`; a scene that never sets `tuning_path` leaves it on the header
## default. Both paths land on 3.0 today, so this asserts the *branch* rather than
## trusting either.
##
## `lock` — the crux. Raw axis deflection to degrees at the front wheels, through
## the real chain, in `valdirone.tscn`. Held stationary on the grid on purpose:
## the steering solve is geometric and speed-independent, so the map from stick to
## lock is the same at 0 and at 140 km/h, and measuring it at rest keeps a terrain
## interaction out of a number that is about input.
##
## `depart` — the reproduction attempt. Full throttle out of the grid slot on the
## real circuit, then the smallest producible stick deflection held for four
## seconds, measuring body slip. It also records which surface each wheel was on
## for every tick, so a departure that happens with four wheels on asphalt is
## separable from one that does not — the second is issue #240 and not this one.

## The shipped circuit, and the featureless flat plane every §6.4 figure in this
## repo was measured on. `--scene=flat` swaps one for the other and changes
## nothing else, which makes the ground the only variable between two runs of the
## same rig — the separation #239 and #240 need in order to be two tickets.
const SCENE_CIRCUIT := "res://scenes/game/valdirone.tscn"
const SCENE_FLAT := "res://scenes/game/proving_ground.tscn"

## How often `depart` samples its trace, in ticks at 120 Hz.
const TRACE_EVERY := 60

## `project.godot` sets both steer actions to this. Read back from the InputMap at
## run time rather than trusted, because a probe that hardcodes the number it is
## checking cannot notice the number moving.
const EXPECTED_DEADZONE := 0.15

## The smallest raw deflection a human can hold past the deadzone.
##
## **estimated.** A DualSense reports its sticks as 8-bit axes, so Godot's -1..1
## maps in steps of 2/255 = 0.00784 from a center code of 128. The first code
## whose magnitude clears 0.15 is 20 codes out: 20 * 2/255 = 0.156863. If macOS's
## GameController framework hands Godot a finer float than the HID report carries,
## the true smallest step is smaller and every figure below only gets smaller with
## it — the direction of the error is safe for the question being asked.
##
## The resolution-free statement is the one that matters and it is in the report:
## at the deadzone edge itself the delivered lock is **exactly zero**, and it
## leaves zero as (raw - 0.15)^3.
const SMALL_STICK := 0.156863

## Full deflection, for the lower bound. A path that delivers nothing is as broken
## as one that delivers too much and fails a one-sided check silently.
const FULL_STICK := 1.0

## What the smallest deflection is allowed to deliver, in degrees of average front
## lock.
##
## **derived**, from two sourced numbers and no judgement. ADR-0072 measured this
## kart's lateral ceiling at 2.10 g. At 140 km/h — the top of the speed range #239
## was reported in, and 38.889 m/s — that ceiling is a radius of
## v^2 / a = 38.889^2 / (2.10 * 9.81) = 73.411 m. The wheelbase is
## `steering.h`'s 1.050 m, which cites `params.py`. So the bicycle-model lock that
## asks the ceiling at 140 km/h is atan(1.050 / 73.411) = **0.819 deg**.
##
## The first cut of this constant took the wheelbase as 2.80 * tan(25 deg) = 1.306,
## reverse-engineered out of `player_driver.h`'s full-lock radius, and got 1.019.
## That was a number nobody sourced: the radius column in that table comes from the
## Ackermann solve and is not `L / tan(inner)`, so backing a wheelbase out of it
## produces something 24% off a figure that is written down two files away. Read
## the constant; do not invert a table to recover it.
##
## So: if the smallest stick deflection delivers less than 0.819 deg of lock, it
## cannot be asking for more grip than the kart has, at any speed the kart can
## reach. That is #239's question stated as a number.
const LIMIT_ASKING_LOCK_DEG := 0.819

## The curve's exponent has to be *measured*, not read off the property, or the
## check cannot tell a cubic branch from a linear one that happens to store 3.0.
## `steering_curve(x)` is compared against `x^gamma` at three interior points.
const CURVE_FIT_TOLERANCE := 1e-9

## How far the *delivered* lock fraction may sit from `strength^gamma`.
##
## **derived, and it is the replay grid and nothing else.** `PlayerDriver` calls
## `replay_snap()` on its own output before `KartBody` ever sees it — ADR-0041,
## quantized upstream of the solver so a live run and a replay consume the same
## numbers. `replay.h` gives steer 32,767 codes over -1..1, so the worst rounding
## is half a code, 1.526e-5. The first cut of this check used 1e-6 and went red at
## 1.31e-5, which is that grid and not a defect. Anything materially larger is a
## second consumer modifying the value between the curve and the solver, and there
## is not supposed to be one.
const SHAPE_TOLERANCE := 2.0e-5

## Ticks to hold each stick position in `lock`. `steer_rate` is 3.4 per second —
## center to full lock in 294 ms — so 90 ticks at 120 Hz is 750 ms and converges
## the rate limiter at every value including full lock. Asserted, not assumed: the
## case fails if the delivered lock is still moving on the last tick.
const HOLD_TICKS := 90

## `depart`'s phases, in ticks at 120 Hz.
const DEPART_ACCEL_TICKS := 900
const DEPART_HOLD_TICKS := 480

## How far the stick has to move the outcome before the input path can be blamed
## for it.
##
## **estimated**, and the reasoning is that the two runs it separates are not close
## together. Measured on the circuit, stick 0.0 and stick 0.156863 produce peak body
## slips that agree to **every printed digit** — 178.778 deg both times — because the
## delivered `input.steer` is bit-identical zero in both. A degree is four orders
## above that agreement and two orders below the departure it would have to explain,
## so nothing lands between them by accident.
const ATTRIBUTION_DEG := 1.0

## What counts as departed. `drive.sh`'s skidpad scenario reaches 170.8 deg of body
## slip and #137 calls that a departure; 30 deg is the same line
## `test_yaw_stability.cpp`'s diagnostic drew.
const DEPART_SLIP_DEG := 30.0

const ALL_CASES := ["deadzone", "gamma", "lock", "depart"]

## The raw deflections `deadzone` sweeps. The three either side of 0.15 are the
## point: a law that rescales and a law that passes through are only distinguished
## near the edge, and the row furthest from the edge agrees under both.
const DEADZONE_SWEEP := [0.0, 0.05, 0.10, 0.140, 0.1484, 0.15, 0.156863, 0.20,
	0.25, 0.30, 0.40, 0.50, 0.575, 0.70, 0.85, 1.0]

## The deflections `lock` walks. Both signs, because the curve preserves sign by
## hand and `input.steer` is a difference of two action strengths.
## `0.1711` is the first raw stick that survives `replay_snap`'s grid and `0.1700`
## is the last that does not — the two rows that pin the dead band. `0.2775`,
## `0.4900` and `0.6770` are the raw sticks that produce the strengths
## `player_driver.h`'s exponent table is written in, so that table's raw-stick
## column is measured here rather than computed in a comment.
const LOCK_SWEEP := [0.156863, 0.1700, 0.1711, 0.20, 0.2775, 0.30, 0.40,
	0.422027, 0.4900, 0.60, 0.6770, 1.0, -0.156863, -0.40, -1.0]

var _args := {}
var _cases: Array = []
var _sabotage := ""
var _stick := SMALL_STICK

var _root: Node
var _kart: KartBody
var _driver: PlayerDriver
var _attached := false
var _tick := 0

var _checks: Array = []
var _lines: Array = []

## Set by `_report`. `quit()` schedules the exit rather than taking it.
var _finished := false

## `deadzone` runs with no scene, so it finishes before anything is instantiated.
var _deadzone_rows: Array = []

## `lock` state.
var _lock_index := 0
var _lock_ticks := 0
var _lock_rows: Array = []
var _lock_prev_deg := 0.0
var _lock_settled := true

## `depart` state.
var _depart_phase := 0
var _depart_ticks := 0
var _depart_peak_slip := 0.0
var _depart_peak_speed := 0.0
var _depart_speed_at_input := 0.0
var _depart_offtrack_ticks := 0
var _depart_surfaces := {}
var _depart_airborne_ticks := 0
var _depart_done := false
var _depart_slip_at_input := 0.0
var _depart_accel_peak_slip := 0.0
var _depart_first_slip_tick := -1
var _depart_first_slip_speed := 0.0
var _depart_hold_slip_tick := -1
var _depart_hold_slip_speed := 0.0
var _depart_trace: Array = []
var _depart_break_position := Vector3.ZERO
var _depart_break_speed := 0.0

## `--reference=<deg>`: the peak body slip the same run reached with the stick at
## exactly zero. Supplied by `stick.sh`, which measures it first.
var _reference := -1.0

## `--coast`: lift at the start of the hold phase instead of staying flat out.
var _coast := false


## Which scene the driving cases run in. `--scene=flat` for the proving ground.
func _scene_path() -> String:
	return SCENE_FLAT if String(_args.get("scene", "circuit")) == "flat" else SCENE_CIRCUIT


func _initialize() -> void:
	_args = _parse_args()
	_stick = float(_args.get("stick", str(SMALL_STICK)))
	_reference = float(_args.get("reference", "-1.0"))
	_coast = _args.has("coast")
	_sabotage = String(_args.get("break", ""))
	if _sabotage != "" and not ["deadzone", "gamma", "dead"].has(_sabotage):
		printerr("--break=%s is not one of deadzone, gamma, dead" % _sabotage)
		quit(1)
		return
	var requested := String(_args.get("case", ""))
	if requested == "":
		_cases = ALL_CASES.duplicate()
	else:
		for name in requested.split(",", false):
			var trimmed := name.strip_edges()
			if not ALL_CASES.has(trimmed):
				printerr("--case=%s is not one of %s" % [trimmed, ", ".join(ALL_CASES)])
				quit(1)
				return
			_cases.append(trimmed)
	print("stick probe: cases %s%s" % [
		", ".join(_cases), "" if _sabotage == "" else "   --break=" + _sabotage,
	])

	# `Input.parse_input_event` **buffers**; it does not dispatch. With
	# `use_accumulated_input` at its default true a synthesized event sits in the
	# queue until the next frame's flush, so every assertion made immediately after
	# a send reads the previous press — and the last press of a run flushes one
	# frame late, which once landed after a fixture had been torn down and quit the
	# whole gate with exit 0 and no report.
	Input.use_accumulated_input = false

	if not ClassDB.class_exists("PlayerDriver"):
		printerr("PlayerDriver is not registered — build the extension: "
			+ "scons target=editor arch=arm64")
		quit(1)
		return

	if _sabotage == "deadzone":
		InputMap.action_set_deadzone(&"steer_left", 0.0)
		InputMap.action_set_deadzone(&"steer_right", 0.0)

	if _cases.has("deadzone"):
		_run_deadzone()

	if not (_cases.has("gamma") or _cases.has("lock") or _cases.has("depart")):
		_report()
		return

	var packed: PackedScene = load(_scene_path())
	if packed == null:
		printerr("could not load " + _scene_path())
		quit(1)
		return
	_root = packed.instantiate()
	get_root().add_child(_root)


# --- the input map ------------------------------------------------------------


## What does `get_action_strength` do to a joypad axis past a deadzone?
##
## Two analytic models, one of which has to be right:
##
##     passthrough   |raw| if |raw| >= dz else 0
##     rescale       clamp((|raw| - dz) / (1 - dz), 0, 1) if |raw| >= dz else 0
##
## They agree at 0 and at 1 and nowhere else, which is why the sweep is dense at
## the edge and why a check that only looked at full deflection would pass under
## either. This is not a preference between the two: the arithmetic that chose
## `steer_gamma` is written against one of them and is wrong if it is the other.
func _run_deadzone() -> void:
	var live_dz := InputMap.action_get_deadzone(&"steer_right")
	# **Both models are built from `EXPECTED_DEADZONE`, not from `live_dz`.** A
	# model that takes its own deadzone from the InputMap is comparing the thing
	# under test against itself: `--break=deadzone` sets it to 0.0, at which point
	# `(raw - 0)/(1 - 0)` is `raw` and the rescale model and the passthrough model
	# are the same function — so the sabotage fitted "rescale" perfectly and the
	# control was MISSED. The check is that the *shipped* 0.15 law is in force.
	var dz := EXPECTED_DEADZONE
	var passthrough_worst := 0.0
	var rescale_worst := 0.0

	for value in DEADZONE_SWEEP:
		var raw: float = value
		# Positive axis is `steer_right`, negative is `steer_left`; both are read so
		# an asymmetry cannot hide. Each value is sent, flushed, and read on the tick
		# after it — see `_send_axis`.
		var right := _read_after_axis(raw)
		var left := _read_after_axis(-raw)
		var passthrough: float = raw if raw >= dz else 0.0
		var rescale: float = clampf((raw - dz) / (1.0 - dz), 0.0, 1.0) if raw >= dz else 0.0
		passthrough_worst = maxf(passthrough_worst, absf(right - passthrough))
		rescale_worst = maxf(rescale_worst, absf(right - rescale))
		_deadzone_rows.append([raw, left, right, passthrough, rescale])

	_send_axis(0.0)

	_line("")
	_line("  deadzone: what get_action_strength returns for joypad axis 0")
	_line("  InputMap deadzone on steer_left/steer_right = %.6f (project.godot ships %.6f)"
		% [live_dz, EXPECTED_DEADZONE])
	_line("  %10s %11s %11s %13s %11s" % ["raw", "left(-)", "right(+)", "passthrough",
		"rescale"])
	for row in _deadzone_rows:
		_line("  %10.6f %11.6f %11.6f %13.6f %11.6f" % row)
	_line("  worst residual vs passthrough %.9f   vs rescale %.9f" % [
		passthrough_worst, rescale_worst,
	])

	# The two models are mutually exclusive, so the check is that exactly one fits.
	# Asserting only "rescale fits" would still pass on a build where both did,
	# which is the shape of a check that cannot fail.
	var fits_rescale := rescale_worst < 1e-6
	var fits_passthrough := passthrough_worst < 1e-6
	_check("the strength law fits exactly one model",
		fits_rescale != fits_passthrough,
		"rescale %s, passthrough %s" % [
			"fits" if fits_rescale else "no", "fits" if fits_passthrough else "no",
		])
	_check("and the one it fits is the rescale", fits_rescale,
		"worst residual %.12f" % rescale_worst)

	# The symmetry the curve's sign handling depends on. `input.steer` is
	# strength(left) - strength(right); an asymmetric law would put a standing bias
	# on a centered stick.
	var asym := 0.0
	for row in _deadzone_rows:
		asym = maxf(asym, absf(row[1] - row[2]))
	_check("the two directions are symmetric", asym < 1e-9, "worst |left-right| %.12f" % asym)
	# The deadzone the shipped project actually carries. Without this a run whose
	# InputMap had been edited would report a law that fits nothing and never say
	# which number moved.
	_check("the steer actions still carry the shipped deadzone",
		absf(live_dz - EXPECTED_DEADZONE) < 1e-6,
		"%.6f, expected %.6f" % [live_dz, EXPECTED_DEADZONE])

	# The edge itself. Under the rescale law this is exactly zero, and that is the
	# resolution-free half of #239's crux number: whatever a stick's quantization
	# is, the first deflection that clears the deadzone delivers nothing.
	var at_edge := _read_after_axis(EXPECTED_DEADZONE)
	_send_axis(0.0)
	_check("the deadzone edge delivers exactly zero", at_edge == 0.0,
		"%.9f at raw %.4f" % [at_edge, EXPECTED_DEADZONE])


## Send one axis position and read it back on the following idle iteration.
##
## `Input.flush_buffered_events()` is what makes the read on the next line see this
## send rather than the one before it. Without it every row of the sweep would be
## shifted by one and the table would still look plausible.
func _send_axis(raw: float) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = 0
	event.axis = JOY_AXIS_LEFT_X
	event.axis_value = raw
	Input.parse_input_event(event)
	Input.flush_buffered_events()


func _read_after_axis(raw: float) -> float:
	_send_axis(raw)
	return Input.get_action_strength(&"steer_right" if raw >= 0.0 else &"steer_left")


# --- the scene ----------------------------------------------------------------


func _physics_process(_delta: float) -> bool:
	# `quit()` schedules the exit; it does not stop this iteration or the next one.
	# Without this guard a `--case=deadzone` run, which never instantiates a scene,
	# reaches the `find_child` below on a null root and prints a script error under
	# a report that already said PASS.
	if _finished:
		return true

	# **Before the attach block, not after it.** `_run_gamma` is called on the tick
	# the nodes are found, so a sabotage applied further down this function had not
	# happened yet when it read the exponent -- `--break=gamma` reported 3.000000
	# and the control was MISSED. The session runner also rewrites `enabled` on
	# every state change, so both of these have to be reapplied every tick anyway.
	if _attached:
		if _sabotage == "gamma":
			_driver.steer_gamma = 1.0
		if _sabotage == "dead":
			_driver.enabled = false

	if not _attached:
		_kart = _root.find_child("Kart", true, false) as KartBody
		_driver = _root.find_child("Driver", true, false) as PlayerDriver
		if _kart == null or _driver == null:
			printerr("scene has no Kart and Driver pair — %s, %s" % [_kart, _driver])
			quit(1)
			return true
		_attached = true
		# Sabotage first, measurement second, and exactly one call to each. An
		# earlier cut ran `_run_gamma` twice — once before the sabotage landed and
		# once after — which appended a green copy and a red copy of every check
		# under the same name, and the fingerprint logic saw the green one and
		# reported the control MISSED. Two rows with one name is its own defect.
		if _sabotage == "gamma":
			_driver.steer_gamma = 1.0
		if _sabotage == "dead":
			_driver.enabled = false
		if _cases.has("gamma"):
			_run_gamma()

	_tick += 1

	# `SessionRunner` holds the driver disabled until the kart has landed and
	# settled — `_apply_input_gate`, one writer. Nothing the stick does reaches the
	# solver before that, so both driving cases wait for it rather than measuring a
	# gate they did not know was shut.
	if not _driver.enabled and _sabotage != "dead":
		if _tick > 1200:
			printerr("the session never released the driver — %d ticks held" % _tick)
			quit(1)
			return true
		return false

	if _cases.has("lock") and not _lock_rows_done():
		return _tick_lock()
	if _cases.has("depart") and not _depart_done:
		return _tick_depart()

	_send_axis(0.0)
	Input.action_release(&"throttle")
	_report()
	return true


## Which branch of the curve is in force, measured in the shipped scene.
##
## The property is printed but not trusted. `steering_curve` is called at three
## interior points and fitted against `x^gamma`; a node storing 3.0 while running
## the linear branch fails here and passes any check that read the property.
func _run_gamma() -> void:
	var gamma: float = _driver.steer_gamma
	var worst_curve := 0.0
	var worst_linear := 0.0
	_line("")
	_line("  gamma: which branch of steering_curve runs in %s" % _scene_path())
	_line("  tuning_path = '%s'   steer_gamma = %.6f" % [_driver.tuning_path, gamma])
	_line("  %8s %14s %14s %14s" % ["x", "curve(x)", "x^gamma", "linear x"])
	for x in [0.25, 0.5, 0.75]:
		var got: float = _driver.steering_curve(x)
		var want := pow(x, gamma)
		worst_curve = maxf(worst_curve, absf(got - want))
		worst_linear = maxf(worst_linear, absf(got - x))
		_line("  %8.4f %14.9f %14.9f %14.9f" % [x, got, want, x])

	_check("the curve is x^steer_gamma", worst_curve < CURVE_FIT_TOLERANCE,
		"worst |curve - x^%.3f| = %.12f" % [gamma, worst_curve])
	# The fingerprint `--break=gamma` has to leave. A linear branch fits x^1.0
	# perfectly and would pass the check above, so the branch is asserted
	# separately and by its consequence rather than by the stored number.
	_check("and it is not the linear branch", worst_linear > 1e-6,
		"worst |curve - x| = %.12f at gamma %.4f" % [worst_linear, gamma])
	# That the value arrived at all. Both the registry default and the header
	# default are 3.0, so this cannot distinguish a wired scene from an unwired
	# one — it pins the number so a change to either has to be deliberate.
	_check("steer_gamma is the registry default 3.0", absf(gamma - 3.0) < 1e-9,
		"%.6f" % gamma)


# --- stick to degrees ---------------------------------------------------------


func _lock_rows_done() -> bool:
	return _lock_index >= LOCK_SWEEP.size()


## Walk the sweep, holding each deflection until the rate limiter has converged,
## and record what reached the front wheels.
##
## Stationary on the grid, deliberately. `steering.h`'s solve is geometric — the
## angle a lock fraction produces does not depend on speed — so this is the same
## map at 140 km/h, measured without a terrain interaction in it.
func _tick_lock() -> bool:
	var raw: float = LOCK_SWEEP[_lock_index]
	if _lock_ticks == 0:
		_send_axis(raw)
	_lock_ticks += 1

	var deg := _front_lock_deg()
	if _lock_ticks < HOLD_TICKS:
		_lock_prev_deg = deg
		return false

	# Converged? A value still moving on the last tick means HOLD_TICKS is too
	# short and every row of this table is wrong by an unknown amount.
	if absf(deg - _lock_prev_deg) > 1e-9:
		_lock_settled = false

	var pair := _front_lock_pair()
	_lock_rows.append([raw,
		Input.get_action_strength(&"steer_left") - Input.get_action_strength(&"steer_right"),
		_driver.get_steer(), _kart.get_steer_input(), pair[0], pair[1]])
	_lock_index += 1
	_lock_ticks = 0

	if _lock_rows_done():
		_send_axis(0.0)
		_finish_lock()
	return false


## Both front wheels, in degrees, as [inner, outer] by magnitude.
##
## **Not the average, and that distinction is load-bearing here.** Ackermann puts
## the two wheels at different angles and `kart_body.cpp` is explicit about which
## one 25 degrees means: *"applied to the inner wheel so no front wheel ever
## exceeds the angle those clearances were measured at"*. At full stick the two
## read 25.00 and 17.37, whose average is 21.18 — so a check written against the
## average and 25.0 fails on correct behavior, which is exactly what the first cut
## of this probe did.
func _front_lock_pair() -> Array:
	var report: Array = _kart.wheel_report()
	# Indexed rather than `get`, because these keys are a contract and
	# `Dictionary.get(key, default)` hides a renamed one forever — that exact shape
	# pinned this scene's front wheels dead straight for a milestone while the
	# solver steered 25 degrees underneath them.
	var fl: float = rad_to_deg(report[0]["steer_angle"])
	var fr: float = rad_to_deg(report[1]["steer_angle"])
	if absf(fl) >= absf(fr):
		return [fl, fr]
	return [fr, fl]


## The tick-to-tick convergence test uses the inner wheel alone.
func _front_lock_deg() -> float:
	return _front_lock_pair()[0]


func _finish_lock() -> void:
	_line("")
	_line("  lock: raw stick to degrees at the front wheels, %s, stationary" % _scene_path())
	_line("  max lock is %.4f deg at the INNER wheel; steer_gamma %.4f" % [
		rad_to_deg(_kart.get_steer_lock()), _driver.steer_gamma,
	])
	_line("  %10s %11s %13s %12s %10s %10s" % ["raw stick", "strength", "input.steer",
		"lock frac", "inner deg", "outer deg"])
	for row in _lock_rows:
		_line("  %10.6f %11.6f %13.9f %12.9f %10.6f %10.6f" % row)

	var small_deg := 0.0
	var full_deg := 0.0
	var shape_worst := 0.0
	for row in _lock_rows:
		var raw: float = row[0]
		if absf(raw - SMALL_STICK) < 1e-9:
			small_deg = absf(row[4])
		if absf(raw - FULL_STICK) < 1e-9:
			full_deg = absf(row[4])
		# The delivered lock fraction must be the strength raised to the exponent.
		# This is where a curve that stopped being applied shows up as a number
		# rather than as a property read.
		var want: float = pow(absf(row[1]), _driver.steer_gamma)
		shape_worst = maxf(shape_worst, absf(absf(row[3]) - want))

	_line("  smallest producible stick %.6f delivers %.6f deg at the inner wheel;"
		% [SMALL_STICK, small_deg]
		+ " the limit-asking lock at 140 km/h is %.3f deg" % LIMIT_ASKING_LOCK_DEG)

	_check("the rate limiter converged inside the hold", _lock_settled,
		"%d ticks per step" % HOLD_TICKS)
	# The upper bound, and #239's question stated as a number. On the inner wheel,
	# which is the larger of the two and therefore the conservative side.
	_check("the smallest stick cannot ask the lateral limit",
		small_deg < LIMIT_ASKING_LOCK_DEG,
		"%.6f deg vs %.3f deg ceiling" % [small_deg, LIMIT_ASKING_LOCK_DEG])
	# The lower bound. Without it a dead input path passes the line above by
	# delivering nothing, which is the failure `--break=dead` exists to prove is
	# caught.
	_check("full stick still reaches full lock", full_deg > 24.9,
		"%.4f deg of %.4f" % [full_deg, rad_to_deg(_kart.get_steer_lock())])
	# The shape, which is what separates a cubic path from a linear one at any
	# stored exponent.
	_check("delivered lock is strength^steer_gamma", shape_worst < SHAPE_TOLERANCE,
		"worst residual %.12f at gamma %.3f, half a steer quantum is %.12f" % [
			shape_worst, _driver.steer_gamma, 0.5 / 32767.0,
		])


# --- the reproduction attempt -------------------------------------------------


## Full throttle out of the grid, then the smallest producible stick deflection
## held for four seconds, on the real circuit.
##
## Everything here goes through the InputMap: the throttle is an action press and
## the steering is a synthesized axis, so `KartBody.input_driver` is never set and
## the whole of `PlayerDriver` is in the loop. That is the difference between this
## and every other driving gate in the repo.
func _tick_depart() -> bool:
	_depart_ticks += 1
	var speed: float = _kart.get_speed_ms()
	_depart_peak_speed = maxf(_depart_peak_speed, speed)
	var slip := absf(_body_slip_deg())

	# `--coast` lifts at the start of the hold phase. It separates a ground that
	# yaws the kart from a power-on instability on a descent, which are two
	# different tickets and read identically in a trace that never lifts.
	if _coast and _depart_phase == 1:
		Input.action_release(&"throttle")
	else:
		Input.action_press(&"throttle", 1.0)

	# The accel phase is measured too, and that is not decoration. The first cut
	# recorded body slip only while the stick was held and could not tell a kart
	# that arrived at the hold already sideways from one the stick upset — and the
	# answer turned out to be neither, which only a trace could show.
	var report: Array = _kart.wheel_report()
	var airborne := 0
	for entry in report:
		var surface: int = entry["surface"]
		_depart_surfaces[surface] = int(_depart_surfaces.get(surface, 0)) + 1
		if not bool(entry["contact"]):
			airborne += 1
	if airborne > 0:
		_depart_airborne_ticks += 1
	if _depart_phase == 0 and slip > DEPART_SLIP_DEG and _depart_first_slip_tick < 0:
		_depart_first_slip_tick = _depart_ticks
		_depart_first_slip_speed = speed
	if _depart_phase == 1:
		_depart_peak_slip = maxf(_depart_peak_slip, slip)
		if slip > DEPART_SLIP_DEG and _depart_hold_slip_tick < 0:
			_depart_hold_slip_tick = _depart_ticks
			_depart_hold_slip_speed = speed
	_depart_accel_peak_slip = maxf(_depart_accel_peak_slip,
		slip if _depart_phase == 0 else 0.0)

	# One trace row every eighth of a second, over both phases. This is what turns
	# "it spun" into "it was straight at 6.0 s and gone by 8.4 s at 138 km/h with
	# four wheels on asphalt", which is the difference between a report and a
	# guess.
	if (_depart_phase * DEPART_ACCEL_TICKS + _depart_ticks) % TRACE_EVERY == 0:
		var min_load := INF
		for entry in report:
			min_load = minf(min_load, float(entry["load"]))
		_depart_trace.append([
			_depart_phase, _depart_ticks / 120.0, speed * 3.6, slip,
			rad_to_deg(_kart.angular_velocity.y), 4 - airborne, _driver.get_steer(),
			min_load, _kart.global_position,
		])

	# Where it let go, for whoever owns the ground. Recorded on the first tick past
	# the line and never overwritten.
	if slip > DEPART_SLIP_DEG and _depart_break_position == Vector3.ZERO:
		_depart_break_position = _kart.global_position
		_depart_break_speed = speed

	if _depart_phase == 0:
		if _depart_ticks < DEPART_ACCEL_TICKS:
			return false
		_depart_phase = 1
		_depart_ticks = 0
		_depart_speed_at_input = speed
		_depart_slip_at_input = slip
		_send_axis(_stick)
		return false

	if _depart_ticks < DEPART_HOLD_TICKS:
		return false

	Input.action_release(&"throttle")
	_send_axis(0.0)
	_depart_done = true
	_finish_depart()
	return false


## Body slip: the angle between where the kart is pointing and where it is going,
## measured on the chassis' own axes. Same definition `test_yaw_stability.cpp`
## uses, so the numbers are comparable to the table in #239.
func _body_slip_deg() -> float:
	var velocity: Vector3 = _kart.linear_velocity
	# **3 m/s, not 0.5.** Below walking pace body slip is not a measurement of
	# anything: this kart spends 1.49 s above slip ratio 0.5 on a full-throttle
	# launch, so the rear steps out at 1.5 m/s and the angle between a velocity of
	# almost nothing and the chassis reads 70 degrees. With the floor at 0.5 the
	# flat-plane control reported "crossed 30 deg at 0.167 s, 5.3 km/h" on a run
	# whose peak once moving was 0.079 deg — a launch artifact wearing a departure's
	# clothes.
	if velocity.length() < 3.0:
		return 0.0
	var basis := _kart.global_transform.basis
	var forward := -basis.z
	var right := basis.x
	return rad_to_deg(atan2(velocity.dot(right), velocity.dot(forward)))


func _finish_depart() -> void:
	var names := {0: "Asphalt", 1: "Kerb", 2: "Verge", 3: "Gravel", 4: "PitLane"}
	var surfaces: Array = []
	for key in _depart_surfaces:
		surfaces.append("%s x%d" % [names.get(key, "surface%d" % key), _depart_surfaces[key]])
	var total_ticks := DEPART_ACCEL_TICKS + DEPART_HOLD_TICKS

	_line("")
	_line("  depart: %s, full throttle then raw stick %.6f held %.2f s%s"
		% [_scene_path(), _stick, DEPART_HOLD_TICKS / 120.0,
		"  (--coast: throttle released for the hold)" if _coast else ""])
	_line("  %8s %8s %10s %10s %11s %8s %13s %10s  %7s %6s %7s" % ["phase", "s",
		"km/h", "slip deg", "yaw deg/s", "on gnd", "input.steer", "min load N",
		"x", "y", "z"])
	for row in _depart_trace:
		var at: Vector3 = row[8]
		_line("  %8s %8.3f %10.2f %10.3f %11.3f %8d %13.9f %10.1f  %7.1f %6.1f %7.1f" % [
			"accel" if row[0] == 0 else "hold", row[1], row[2], row[3], row[4],
			row[5], row[6], row[7], at.x, at.y, at.z,
		])
	_line("  speed when the stick went on %.2f m/s (%.1f km/h), peak %.1f km/h"
		% [_depart_speed_at_input, _depart_speed_at_input * 3.6, _depart_peak_speed * 3.6])
	_line("  body slip at that moment %.3f deg; worst during the accel phase %.3f deg"
		% [_depart_slip_at_input, _depart_accel_peak_slip])
	_line("  peak body slip while the stick was held %.3f deg   departure line %.1f deg"
		% [_depart_peak_slip, DEPART_SLIP_DEG])
	if _depart_first_slip_tick >= 0:
		_line("  crossed %.0f deg during the ACCEL phase at %.3f s, %.1f km/h — before the "
			% [DEPART_SLIP_DEG, _depart_first_slip_tick / 120.0,
			_depart_first_slip_speed * 3.6]
			+ "stick moved at all")
	if _depart_hold_slip_tick >= 0:
		_line("  crossed %.0f deg during the HOLD phase at %.3f s, %.1f km/h"
			% [DEPART_SLIP_DEG, _depart_hold_slip_tick / 120.0,
			_depart_hold_slip_speed * 3.6])
	_line("  wheel-ticks by surface over both phases: %s" % ", ".join(surfaces))
	_line("  ticks with a wheel off the ground: %d of %d" % [
		_depart_airborne_ticks, total_ticks,
	])
	if _depart_break_position != Vector3.ZERO:
		_line("  first past %.0f deg at world (%.1f, %.1f, %.1f), %.1f km/h" % [
			DEPART_SLIP_DEG, _depart_break_position.x, _depart_break_position.y,
			_depart_break_position.z, _depart_break_speed * 3.6,
		])

	_check("the kart reached a speed worth asking the question at",
		_depart_speed_at_input > 20.0, "%.1f km/h" % (_depart_speed_at_input * 3.6))
	# The check #239 asks for, and it is deliberately about the *stick*: the
	# departure has to be attributable to the input for this ticket to own it. The
	# `zero` reference run is what decides that, and it is a separate check rather
	# than a note, because a run that departs identically at stick 0.0 has proved
	# the input path innocent and belongs to somebody else.
	_check("#240: the kart held a straight line on this ground",
		_depart_peak_slip < DEPART_SLIP_DEG,
		"%.3f deg peak body slip" % _depart_peak_slip)
	# **The check #239 actually owns.** Whether the kart spun is not this ticket's
	# question — whether the *stick* spun it is. With a reference run at stick 0.0
	# in hand, an outcome that does not move when the stick moves is an input path
	# that had nothing to do with it, and a departure that only appears with the
	# stick is this ticket's defect. Without the reference this check does not run
	# at all rather than quietly passing, which is the difference between a check
	# and a decoration.
	if _reference >= 0.0:
		var delta := absf(_depart_peak_slip - _reference)
		# Green means the input path is innocent, and the polarity is deliberate:
		# a gate whose green means "the defect is here" is backwards, and the first
		# cut of this check was written that way and read as a pass when the stick
		# spun the kart.
		_check("#239: the input path is not what departed the kart",
			delta < ATTRIBUTION_DEG,
			"peak %.3f deg vs %.3f deg at stick 0.0, delta %.3f deg"
				% [_depart_peak_slip, _reference, delta])


# --- plumbing -----------------------------------------------------------------


func _line(text: String) -> void:
	_lines.append(text)


func _check(name: String, ok: bool, detail: String) -> void:
	_checks.append([name, ok, detail])


func _parse_args() -> Dictionary:
	var out := {}
	for argument in OS.get_cmdline_user_args():
		if not argument.begins_with("--"):
			continue
		var body := argument.substr(2)
		var split := body.split("=", false, 1)
		out[split[0]] = split[1] if split.size() > 1 else "true"
	return out


func _report() -> void:
	_finished = true
	for text in _lines:
		print(text)
	print("")
	var failures := 0
	for check in _checks:
		var ok: bool = check[1]
		failures += 0 if ok else 1
		print("check %-48s %s   %s" % [check[0], "PASS" if ok else "FAIL", check[2]])
	print("stick probe: %d checks, %d failed" % [_checks.size(), failures])

	if _sabotage != "":
		# Inverted, and it demands the saboteur's own fingerprint rather than
		# accepting any red. A control that passes off a pre-existing failure is
		# not a control — the first cut of `shell.sh`'s reported "caught" off a red
		# it had not caused.
		var wanted := _sabotage_fingerprint()
		var caught: bool = wanted[0]
		print("negative control --break=%s: %s (%s)" % [
			_sabotage, "CAUGHT" if caught else "MISSED", wanted[1],
		])
		_scrub()
		quit(0 if caught else 1)
		return

	if failures > 0:
		printerr("the stick -> input.steer path does not behave as #239's gate asserts")
	_scrub()
	quit(0 if failures == 0 else 1)


## Which named checks each sabotage must take down, and no others of that name
## passing.
##
## Naming the checks rather than counting failures is the whole point: a run that
## went red for an unrelated reason would count the same and report a control that
## never fired.
func _sabotage_fingerprint() -> Array:
	var wanted: Array = []
	match _sabotage:
		"deadzone":
			wanted = ["and the one it fits is the rescale",
				"the deadzone edge delivers exactly zero"]
		"gamma":
			# Deliberately NOT "delivered lock is strength^steer_gamma": that check
			# compares against the exponent the node currently holds, so a linear
			# path fits x^1.0 exactly and it stays green -- correctly. A fingerprint
			# naming it would demand a red the sabotage does not and should not
			# cause. It belongs to `dead`, where the delivered value is zero.
			wanted = ["and it is not the linear branch",
				"steer_gamma is the registry default 3.0"]
		"dead":
			wanted = ["full stick still reaches full lock"]
	var missing: Array = []
	var still_green: Array = []
	for name in wanted:
		var found := false
		for check in _checks:
			if check[0] == name:
				found = true
				if bool(check[1]):
					still_green.append(name)
		if not found:
			missing.append(name)
	if not missing.is_empty():
		return [false, "these checks did not run: " + ", ".join(missing)
			+ " — pass the case that owns them"]
	if not still_green.is_empty():
		return [false, "still green under sabotage: " + ", ".join(still_green)]
	return [true, "%d fingerprint checks went red" % wanted.size()]


## Every worktree shares one `user://`. This probe writes nothing of its own, but
## `circuit.gd` builds a `KartGhost` recorder whose directory is a hardcoded
## `user://ghosts` with no `set_base_dir`, and the run must not leave one behind.
## Nothing is saved unless a lap closes and this probe never closes one, so this is
## a belt-and-braces sweep of the private directory the run was pointed at.
func _scrub() -> void:
	var base := "user://stick_probe"
	if not DirAccess.dir_exists_absolute(base):
		return
	var dir := DirAccess.open(base)
	if dir == null:
		return
	for name in dir.get_files():
		dir.remove(name)
