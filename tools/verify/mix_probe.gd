extends SceneTree

## What the mix sounds like while the kart is MOVING. Issue #242 area 4.
##
##     godot --headless --audio-driver CoreAudio --path . \
##         --script tools/verify/mix_probe.gd -- --throttle=0 --grass=false
##     tools/verify/mix.sh                  # the same thing with the controls
##
## ## Why this exists when two audio level probes already do
##
## Neither of them has ever heard a kart move.
##
## `audio_level_probe.gd` measures the transfer function of Godot's 3D path with a
## reference sine, and then measures the shipped rig on a `KartBody` **with no
## collision shape, falling**, at one held throttle. `scrub_cost_probe.gd` measures
## what the noise layers cost inside `_mix` at held operating points. Both are the
## right instrument for their question and neither can answer this one: every gain
## in `engine_voice_rig.gd` was chosen against a level measured at a single
## operating point, and the balance between six layers is a function of speed, rpm,
## gear and surface. Anthony's report after the first driving session was "nothing
## covers what is on screen or in my ears while the kart moves".
##
## So this probe drives `proving_ground.tscn` -- the scene every §6.4 figure is
## measured on -- holds it at a sequence of real road speeds with a controller, and
## measures each layer of the shipped mix separately at each one.
##
## ## The protocol, stated because every number below depends on it
##
##   * **Ground: the proving ground's flat asphalt plane**, 2,400 m square, no
##     elevation, no camber, no walls. Every driving figure in this repo was
##     measured on it and CLAUDE.md's standing memory is that nobody knew. Said
##     here so the next reader does.
##   * **Speed is held by a proportional controller**, not scripted open loop.
##     `drive_probe.gd` is open loop on purpose, because it hashes state and a
##     controller reading its own noisy state is not bit-reproducible. This probe
##     hashes nothing and needs the kart to actually BE at 20 m/s, so it closes the
##     loop and prints the speed it achieved in every cell rather than the one it
##     asked for.
##   * **Layers are separated by muting, not by stopping.** `stop()` re-instantiates
##     the playback, which reseeds every filter and every PRNG and costs a settling
##     transient in a window this short. `volume_db = MUTE_DB` leaves all six synths
##     running continuously and only changes what reaches the bus. What that costs
##     is leakage, and leakage is measured rather than assumed: every speed group
##     carries a `floor` cell with all six muted, and the check demands the floor
##     sit `FLOOR_MARGIN_DB` below the quietest layer in that group.
##   * **The capture is downstream of the kart bus master and upstream of Master.**
##     ADR-0039: an `AudioEffectCapture` sits upstream of its own bus's volume, so
##     the Kart bus is re-sent into the Probe bus and Master is trimmed to keep the
##     room quiet without moving a figure. The calibration row is the proof.
##
## ## The six layers, and which of them are positional
##
##     EngineVoice   AudioStreamPlayer3D   engine mount    unit 3.0  max 150
##     ScrubVoice    AudioStreamPlayer3D   rear axle       unit 2.0  max  60
##     ShiftVoice    AudioStreamPlayer3D   engine mount    unit 3.0  max  90
##     RollVoice     AudioStreamPlayer3D   rear axle       unit 2.0  max  30
##     WindVoice     AudioStreamPlayer     not in the world at all
##
## A positional player is 3.19 dB down in total power against a plain one at the
## same gain (ADR-0039), so **the wind layer collects 3 dB the other four do not**.
## That is not folded into anything below; it is one of the reasons the wind row
## reads where it reads, and it is printed in the header so the reader can subtract
## it if they want the synth-side comparison instead of the mix-side one.
##
## ## What is asserted and what is only reported
##
## Asserted, with a `--break` control each:
##
##     capture-honest      a full-scale sine on the probe bus reads 0.00 dBFS
##     kart-moves          every cell reached its target speed
##     solo-isolates       the four steady layers, summed in power, equal the mix
##     mute-floor          the all-muted floor sits far below the full mix
##     mix-level           the whole mix sits inside a -40..0 dBFS sanity band
##     listener-pinned     chase, cockpit and free hear the same mix
##     surface-responds    the rolling layer changes with the surface, and the
##                         curb's rumble rate is the one `surface.h` implies
##
## Reported and NOT asserted: every level, every balance, every crossover speed.
## Those are Anthony's call -- `engine_voice_rig.gd` says each gain is "estimated
## and owed an ear", and a gate that asserted one would be a gate that had made the
## judgement. This wave's rule is that a constant somebody dislikes is a proposal
## with numbers.
##
## ## Why CoreAudio only
##
## Same refusal `scrub_cost_probe.gd` makes. ADR-0035 measured the Dummy driver
## reaching `_mix` in irregular bursts on an ordinary thread; a capture buffer
## drained on a physics tick against a mixer that arrives in bursts is a sampling
## problem nobody has characterized, and this probe's whole output is that sampling.

const SCENE_PATH := "res://scenes/game/proving_ground.tscn"

## Bus the measurement is taken on, and the trim Master is held at while it runs.
##
## Every figure is computed upstream of Master, which is what makes the trim free.
## The calibration cell is the check on that claim and not a comment about it.
const PROBE_BUS := "MixProbe"
const MASTER_TRIM_DB := -60.0

## What a muted player is set to, and how far below the quietest measured layer the
## all-muted floor has to sit for the solo decomposition to mean anything.
##
## -80 rather than -60 because the loudest and quietest layers in this mix are
## expected to be 30-40 dB apart, and a mute that is only 60 dB down would put the
## loudest layer's leakage within 20 dB of the quietest layer's real level.
const MUTE_DB := -80.0

## How far above the all-muted floor a row has to sit before its level is quoted as
## the layer's own rather than as leakage.
##
## 10 dB, and it is arithmetic rather than taste: a floor 10 dB down contributes 10%
## of the measured power, which is 0.41 dB of error on the row. Everything in this
## report is quoted to a hundredth and compared at whole dB, so 0.41 dB is inside
## the precision anything is argued at. 20 dB would have been the tidy answer and it
## would have thrown away the scrub layer at 35 m/s, which sits 11.3 dB up and is
## real -- a kart running straight at 126 km/h has a degree of slip in it.
const FLOOR_MARGIN_DB := 10.0

## How far below the full mix the all-muted floor has to sit for the per-layer
## decomposition to mean anything.
##
## 40 dB, which is 0.01% of a row's power -- three orders of magnitude below
## anything argued about here. The shipped mute measures 70 dB down, so this is not
## a threshold the instrument is scraping past; it is set where a real regression in
## the mute would be caught long before it moved a published figure.
const MUTE_DEPTH_DB := 40.0

## How close to the all-muted floor a layer has to be before its row is reported as
## silent rather than as a level. 1.5 dB: two windows on a stochastic noise floor
## differ by a few tenths and never by this much.
const SILENT_BAND_DB := 1.5

## Sentinel for "this group has no all-muted floor cell to compare against". A
## value no real dB difference can take, because -1.0 is a perfectly ordinary one
## and a sentinel that collides with a measurement is a sentinel that lies.
const NO_FLOOR := -999.0

## Road speeds the level table is taken at, m/s.
##
## 5 to 35 in 5s. The top is 35 and not 38.9 because 38.9 m/s is the §6.4 top speed
## and a controller cannot hold a kart AT its terminal velocity -- there is no
## throttle margin left, so the cell would measure whatever the kart drifted to.
## 35 m/s is 126 km/h and is inside the envelope with room to correct.
const SPEEDS: Array[float] = [5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 35.0]

## Road speeds the cornering protocol is taken at, and the steer held there.
##
## **0.35 and not full lock, deliberately.** #137 is open: the skidpad scenario in
## `drive.sh` departs at 170.8 degrees of body slip and the steering answers 0.489
## of what the front wheels are pointed at. A cell that spun would measure the scrub
## of a kart that is no longer driving, so the steer is modest and the achieved
## body slip and slip angle are printed in the table beside the level. If a cell
## departs anyway the row says so rather than being averaged into a number.
const CORNER_SPEEDS: Array[float] = [10.0, 20.0, 30.0]
const CORNER_STEER := 0.35

## Speed the surface comparison is taken at, and the four surfaces.
##
## 20.0 because that is the speed `roll_audio.h`'s own acceptance table was
## measured at -- four surfaces at a constant 20 m/s, spread 3.94 dB at
## `roll_gain = 0.09`. Using the same speed makes this an in-engine re-measurement
## of a published offline figure rather than a new number nobody can compare.
const SURFACE_SPEED := 20.0
const SURFACE_NAMES: Array[String] = ["asphalt", "curb", "grass", "dirt"]

## The curb's ripple wavelength, meters, from `src/core/surface.h`'s SURFACE_CURB
## row.
##
## **Retyped on purpose**, exactly as `shift_probe.gd` retypes it and for the same
## reason: the check compares the rate the audio thread publishes against `v/lambda`
## computed independently here. Reading it back out of the same class would be
## asking the class to agree with itself.
const CURB_WAVELENGTH_M := 0.15

## Speed the camera comparison is taken at.
const CAMERA_SPEED := 20.0

## Where the three camera modes put a camera, from the shipped rigs.
##
## `chase_camera.gd` ARM_LENGTH 3.4 and ARM_HEIGHT 1.05, which is 3.56 m from the
## kart origin -- the same distance `audio_level_probe.gd` calls "the chase camera"
## and the only distance this mix had ever been judged at before #160.
## `cockpit_camera.gd` sits at `KartBody::driver_head_position()`. The free camera
## has no fixed distance; 25 m is a plausible one and is marked `estimated`.
const CHASE_ARM_LENGTH := 3.4
const CHASE_ARM_HEIGHT := 1.05
const FREE_DISTANCE := 25.0

## How far the two camera modes may differ before `listener-pinned` goes red, dB.
##
## 0.5 and not 0.0 because the two cells are two different windows on a moving kart:
## the controller is holding a speed rather than pinning one, and the synth's own
## state advances between them. A real listener swing is 20.7 dB (ADR-0039) and is
## nowhere near this threshold from either side.
const CAMERA_SPREAD_LIMIT_DB := 0.5

## How much of a spread across the four surfaces the rolling layer must show.
##
## 1.0 dB. `roll_audio.h` publishes 3.94 dB measured offline through the real
## synths at `roll_gain = 0.09`, and before that layer existed the spread was
## 0.03 dB. The threshold is set well below the published figure and well above the
## broken one, so it is a check on the JOIN rather than a re-litigation of the gain.
const SURFACE_SPREAD_LIMIT_DB := 1.0

## Ticks per phase. 120 Hz, so 72 ticks is 0.6 s.
##
## The settle window covers the controller's own correction after a target change
## and the mute ramps; the measure window is long enough that a 100 Hz rumble has 60
## cycles in it. Both are short enough that a kart holding 20 m/s travels 12 m in a
## cell, which keeps the whole run on the plane.
const SETTLE_TICKS := 72
const MEASURE_TICKS := 72

## Ticks the controller is given to reach a new target before the cell is measured
## anyway and marked as not reached. 900 at 120 Hz is 7.5 s; 0-35 m/s takes about 5.
const SEEK_BUDGET_TICKS := 900

## How close to the target counts as arrived, m/s, and for how many consecutive
## ticks. Half a meter per second is 1.8 km/h.
const SEEK_TOLERANCE_MS := 0.30
const SEEK_HOLD_TICKS := 15

## Fraction of the target speed a cell must reach for `kart-moves` to pass.
const SPEED_REACHED_FRACTION := 0.90

## Controller gains, and the clamp on the integrator.
##
## Proportional alone leaves a steady-state offset proportional to drag -- measured
## 0.57 m/s low at a 20 m/s target, which is 3% there and 11% at the 5 m/s cell, and
## the low-speed cells are where the clutch and the roll layer are changing fastest
## with speed. The integrator removes it. It is **clamped**, not free, because the
## surface cells drop the grip multiplier to 0.18 and an unclamped integrator on a
## kart that cannot reach its target winds up and then overshoots into the next
## cell, which is a worse artifact than the offset it was fixing.
const THROTTLE_GAIN := 0.60
const BRAKE_GAIN := 0.25
const INTEGRAL_GAIN := 0.80
const INTEGRAL_CLAMP := 0.50

## Ticks the steer is ramped in over, entering a cornering cell. A step to 0.35
## would be a snap input, which is a different transient from a corner.
const STEER_RAMP_TICKS := 60

## How far from spawn the kart may travel before it is put back. The plane is
## 2,400 m square and spawn is at z = +1000, so there is 2,200 m of run; 900 keeps
## a wide margin for a cornering cell that is not going where it looks.
const RESPAWN_DISTANCE_M := 900.0

## Reference frequency for the calibration sine, and the gain it is played at.
## `AudioProbeStream` with one partial at unity gain is a full-scale sine --
## `partial_gain(1)` is `1^k`, exactly 1.0 for any exponent.
const REFERENCE_HZ := 500.0

## Which check each `--break` mode must turn red. The verdict demands this specific
## one, because CLAUDE.md records `shell.sh`'s first controls reporting "caught" off
## a pre-existing red they had not caused.
const BREAK_EXPECT := {
	"capture": "capture-honest",
	"solo": "solo-isolates",
	"mute": "mute-floor",
	"static": "kart-moves",
	"level": "mix-level",
	"listener": "listener-pinned",
	"surface": "surface-responds",
}

## The WAV passes, in order. Each is [key, seconds, one line saying what to listen
## for]. Anthony judges by ear and that judgement stays in the main thread; these
## exist so he has something to judge.
const WAV_PASSES: Array = [
	["drive", 15.0,
		"standing start, full throttle, straight, asphalt, as the DRIVER hears it. "
		+ "Listen for: does the note rise through the gears, and can you hear the shifts."],
	["drive-no-listener", 15.0,
		"the same run with the AudioListener3D cleared, so the chase camera is the "
		+ "listener. This is what #160 fixed. Listen for: how much further away the "
		+ "whole kart is, and whether the wind moves with it."],
	["corner", 12.0,
		"held at 20 m/s with 0.35 steer. Listen for: whether tire scrub is audible "
		+ "at all under the engine, and whether it tells you the rear is going."],
	["surfaces", 12.0,
		"held at 20 m/s, ground flipped asphalt -> curb -> grass -> dirt every 3 s. "
		+ "Listen for: four different surfaces, and the curb's rumble rate."],
	["layers", 15.0,
		"held at 25 m/s, each layer soloed for 2.5 s in turn: engine, scrub, wind, "
		+ "roll, shift (two clacks), then all six. Listen for: which layers you "
		+ "cannot hear in the mix even though they are there."],
]


var _root: Node
var _kart: KartBody
var _ground: Node
var _listener: AudioListener3D
var _camera: Camera3D
var _capture: AudioEffectCapture

## The five players, by the names `EngineVoiceRig` gives them.
var _players := {}
var _streams := {}

var _cases: Array = []
var _break := ""
var _out_dir := ""
var _speeds: Array[float] = []

var _cells: Array = []
var _cell := -1
var _phase := "seek"
var _phase_ticks := 0
var _seek_hits := 0
var _tick := 0
var _attached := false

## Measurement accumulators, reset per window.
var _sum_left := 0.0
var _sum_right := 0.0
var _frames := 0
var _peak := 0.0
var _sum_x2 := 0.0
var _sum_dx2 := 0.0
var _prev_x := 0.0

## Operating-point accumulators, reset per window.
var _op_ticks := 0
var _op_speed := 0.0
var _op_rpm := 0.0
var _op_slip_deg := 0.0
var _op_gear := 0
var _op_speed_min := 1.0e9
var _op_speed_max := -1.0e9
var _op_body_slip_deg := 0.0

var _rows: Array = []
var _failures: Array[String] = []
var _checks: Array = []
var _notes: Array[String] = []

var _mix_rate := 48000.0
var _tick_hz := 120.0
var _calibration_db := 0.0
var _calibration_seen := false

## The calibration emitter. Only playing during its own cell.
var _probe_node: Object
var _reference_player: AudioStreamPlayer

## WAV state.
var _wav_index := -1
var _wav_samples := PackedFloat32Array()
var _wav_ticks := 0
var _wav_target_ticks := 0
var _wav_written: Array = []
var _wav_clipped := 0
var _wav_peak := 0.0

## Controller state.
var _target_speed := 0.0
var _steer_target := 0.0
var _steer_now := 0.0
var _throttle_now := 0.0
var _brake_now := 0.0
var _forced_throttle := -1.0
var _shift_strikes_before := 0
var _camera_mode := ""
var _cell_reached := true
var _speed_integral := 0.0


func _initialize() -> void:
	var args := Cmdline.parse()
	_cases = Array(Cmdline.as_string(args, "case", "level,corner,surface,camera")
		.split(",", false))
	_break = Cmdline.as_string(args, "break", "")
	_out_dir = Cmdline.as_string(args, "out", "")
	if _break != "" and not BREAK_EXPECT.has(_break):
		printerr("error: unknown --break mode '%s'" % _break)
		printerr("       modes: %s" % ", ".join(BREAK_EXPECT.keys()))
		quit(2)
		return

	var speeds_arg := Cmdline.as_string(args, "speeds", "")
	if speeds_arg == "":
		_speeds = SPEEDS.duplicate()
	else:
		for piece in speeds_arg.split(",", false):
			_speeds.append(float(piece))

	if AudioServer.get_driver_name() == "Dummy":
		printerr("error: the audio driver is Dummy. ADR-0035 measured it reaching _mix in")
		printerr("       irregular bursts on an ordinary thread, and this probe's whole")
		printerr("       output is a capture buffer drained on a physics tick against that")
		printerr("       mixer. Run with --headless --audio-driver CoreAudio.")
		quit(1)
		return

	for required in ["KartBody", "EngineVoiceStream", "NoiseVoiceStream",
			"AudioProbe", "AudioProbeStream"]:
		if not ClassDB.class_exists(required):
			printerr("error: %s is not registered -- scons target=editor arch=arm64" % required)
			quit(1)
			return

	_mix_rate = AudioServer.get_mix_rate()
	_tick_hz = float(Engine.physics_ticks_per_second)

	# Before anything is built, so no block is ever rendered at full level through a
	# real device on a real desk.
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("Master"), MASTER_TRIM_DB)

	# **Before the scene, and the order is load-bearing.** An `AudioServer` bus may
	# only send to a bus with a LOWER index, because the server mixes from the
	# highest index down to Master. `KartRig` creates the Kart bus from the scene's
	# `_ready`, which in a `--script` main loop runs on the first frame and therefore
	# after `_initialize`; building the probe bus in `_attach` put it ABOVE the Kart
	# bus and `set_bus_send` silently did nothing. Every layer then read exactly
	# -180.00 dBFS with a clean-looking table and a calibration row that still said
	# 0.00, which is the same shape of failure ADR-0039 records for a capture on the
	# wrong side of a volume. `audio_level_probe.gd` guards it with `if kart_index >
	# index`; this file makes the ordering true instead, and `_route_kart_bus`
	# refuses rather than degrading if it ever stops being true.
	_build_bus()

	var packed: PackedScene = load(SCENE_PATH)
	if packed == null:
		printerr("error: could not load %s" % SCENE_PATH)
		quit(1)
		return
	_root = packed.instantiate()
	get_root().add_child(_root)


## The scene is not in the tree during `_initialize` -- CLAUDE.md's trap, and
## `drive_probe.gd` and `shoot.gd` both work around it the same way. Everything the
## scene builds exists by the first `_physics_process`.
func _attach() -> bool:
	_kart = _root.find_child("Kart", false, false) as KartBody
	if _kart == null:
		printerr("error: no Kart in the scene -- is assets/generated/kart.glb built?")
		return false

	_ground = _root.find_child("Ground", true, false)
	if _ground == null:
		printerr("error: no Ground body in the scene; the surface cells cannot run.")
		return false

	for entry in [["engine", "EngineVoice"], ["scrub", "ScrubVoice"],
			["wind", "WindVoice"], ["shift", "ShiftVoice"], ["roll", "RollVoice"]]:
		var node := _kart.get_node_or_null(NodePath(entry[1]))
		if node == null:
			printerr("error: the kart has no %s player. EngineVoiceRig.attach_noise "
				% entry[1] + "returns null when the extension is absent.")
			return false
		_players[entry[0]] = node
		_streams[entry[0]] = node.stream

	_listener = _kart.get_node_or_null(NodePath("DriverEars")) as AudioListener3D
	if _listener == null:
		printerr("error: the kart has no DriverEars listener. KartRig builds one; a "
			+ "scene without it is the pre-#160 mix and this probe cannot tell the "
			+ "two apart.")
		return false

	if not _route_kart_bus():
		return false

	# The probe's own camera, made current in every camera cell. Built rather than
	# borrowed so that the three positions are exactly the geometry named in the
	# constants above and not whatever the scene's chase spring happened to have
	# settled to when the window opened.
	_camera = Camera3D.new()
	_camera.name = "MixProbeCamera"
	get_root().add_child(_camera)

	_build_reference()

	# ADR-0040: a valid `input_driver` Callable overrides pushed input, and this is
	# the only way to close a loop around the solver from a probe. The scene was
	# launched with `--throttle=0`, which is what makes `AssistSettings` skip the
	# real `user://settings.cfg` -- every worktree shares one, and a probe that read
	# a driver's stored auto-shift preference would be a probe whose numbers moved
	# when he pressed G in another session.
	_kart.input_driver = func() -> Dictionary:
		return {"throttle": _throttle_now, "brake": _brake_now, "steer": _steer_now}

	# Stated rather than inherited. Both are the shipped defaults, but a table whose
	# gear column depends on a default nobody wrote down is a table that changes
	# meaning the day somebody changes the default. Printed in the header too.
	_kart.auto_shift = true
	_kart.auto_clutch = true

	_build_cells()
	return true


## The probe bus, with the kart bus chained into it.
##
## ADR-0039, measured: an `AudioEffectCapture` sits UPSTREAM of its own bus's
## volume, so a capture on the bus carrying the master cannot see the master. The
## kart bus is re-sent here instead, which puts the capture downstream of
## `MASTER_GAIN_DB` and measures the graph that ships. The same fact is what lets
## Master be trimmed 60 dB without moving a single figure, and the calibration cell
## is the check on that rather than a comment about it.
func _build_bus() -> void:
	var index := AudioServer.bus_count
	AudioServer.add_bus(index)
	AudioServer.set_bus_name(index, PROBE_BUS)
	AudioServer.set_bus_send(index, "Master")

	if _break == "capture":
		# The saboteur: 12 dB of attenuation inserted BEFORE the capture in the same
		# bus's effect chain. Every level in the report comes out 12 dB low and the
		# calibration row says so, which is the fingerprint. This is the shape of the
		# defect ADR-0039 records as having shipped once already -- a level probe
		# built so that it reports the whole chain minus the one number it exists to
		# set, silently, with a clean-looking table.
		var amp := AudioEffectAmplify.new()
		amp.volume_db = -12.0
		AudioServer.add_bus_effect(index, amp)

	_capture = AudioEffectCapture.new()
	# 0.5 s. At 120 Hz a tick is 400 frames at 48 kHz; a 0.5 s ring is 24,000, so a
	# tick cannot overflow it and a splice of two operating points cannot be read as
	# a smooth curve.
	_capture.buffer_length = 0.5
	AudioServer.add_bus_effect(index, _capture)


## Chain the kart bus into the probe bus, and refuse if the index order forbids it.
##
## Called after the scene's `_ready` has created the Kart bus, which is the only
## moment the index is knowable. The refusal is the point: a silently dropped send
## is a report full of -180 dBFS rows next to a calibration row reading 0.00, and
## that is a table a reader trusts.
func _route_kart_bus() -> bool:
	var kart_index := AudioServer.get_bus_index(EngineVoiceRig.KART_BUS)
	var probe_index := AudioServer.get_bus_index(PROBE_BUS)
	if kart_index < 0:
		printerr("error: the '%s' bus does not exist. EngineVoiceRig.ensure_bus falls "
			% EngineVoiceRig.KART_BUS + "back to Master when it cannot create it.")
		return false
	if kart_index <= probe_index:
		printerr("error: the Kart bus is at index %d and the probe bus at %d. A bus may "
			% [kart_index, probe_index]
			+ "only send to a LOWER index, so this send would be dropped silently and "
			+ "every layer would read -180 dBFS beside a calibration row of 0.00.")
		return false
	AudioServer.set_bus_send(kart_index, PROBE_BUS)
	if _break == "level":
		# +20 dB on the kart bus master. Must be caught by `no-clip` and by nothing
		# else: the calibration player is on the probe bus directly and never sees
		# it, so `capture-honest` stays green and the red is attributable.
		AudioServer.set_bus_volume_db(kart_index, EngineVoiceRig.MASTER_GAIN_DB + 20.0)
	return true


## The calibration emitter: one partial at unity gain is a full-scale sine, on a
## plain `AudioStreamPlayer` straight onto the probe bus. It reads 0.00 dBFS or the
## whole report is off by whatever it reads.
func _build_reference() -> void:
	_probe_node = ClassDB.instantiate("AudioProbe")
	_probe_node.set_mix_rate(_mix_rate)
	_probe_node.set_engine_synth(false)
	_probe_node.set_scrub_wind(false, false)
	_probe_node.set_physics_busy_us(0)
	_probe_node.set_use_table(false)
	_probe_node.set_partials(1)
	_probe_node.set_gain(1.0)
	_probe_node.set_fundamental_hz(REFERENCE_HZ)
	get_root().add_child(_probe_node)
	_probe_node.arm()

	_reference_player = AudioStreamPlayer.new()
	_reference_player.name = "MixProbeReference"
	_reference_player.stream = ClassDB.instantiate("AudioProbeStream")
	_reference_player.bus = PROBE_BUS
	get_root().add_child(_reference_player)


# --- the cell list ------------------------------------------------------------

func _cell_new(group: String, label: String, layer: String, target: float) -> Dictionary:
	return {
		"group": group, "label": label, "layer": layer, "target": target,
		"steer": 0.0, "surface": 0, "camera": "", "listener": true, "event": "",
	}


func _build_cells() -> void:
	# Always first, and always run. Everything else is a difference against it.
	var calibration := _cell_new("0 calibration", "full-scale sine, non-positional",
		"reference", 0.0)
	_cells.append(calibration)

	if _cases.has("level"):
		for speed in _speeds:
			var group := "1 level vs speed, straight, asphalt"
			# `shift` is in the steady list as well as having its own event cell
			# below, and it has to be: the layer carries the CLUTCH noise as well as
			# the clack, and a launch is exactly where clutch slip lives. Leaving it
			# out made the power decomposition miss 18.7 dB at 5 m/s and report it as
			# a broken mute.
			for layer in ["engine", "scrub", "wind", "roll", "shift", "all", "floor"]:
				_cells.append(_cell_new(group, "%s @ %.0f m/s" % [layer, speed],
					layer, speed))
			# The shift layer is EVENT driven: its `_mix` runs on every block and
			# renders exact silence when no shift has happened, so a steady-state
			# window measures nothing and would print a confident -inf. This cell
			# commands a downshift and an upshift inside its own measure window and
			# reports the PEAK, which is the number that means anything for a 9 ms
			# transient. `strikes` from the audio thread is the fingerprint that the
			# clacks were struck rather than that a level was reached some other way.
			var shift_cell := _cell_new(group, "shift EVENT @ %.0f m/s" % speed,
				"shift", speed)
			shift_cell["event"] = "shift"
			_cells.append(shift_cell)

	if _cases.has("corner"):
		for speed in CORNER_SPEEDS:
			for layer in ["engine", "scrub", "all", "floor"]:
				var cell := _cell_new("2 level vs speed, steer %.2f, asphalt" % CORNER_STEER,
					"%s @ %.0f m/s" % [layer, speed], layer, speed)
				cell["steer"] = CORNER_STEER
				_cells.append(cell)

	if _cases.has("surface"):
		for surface in range(SURFACE_NAMES.size()):
			for layer in ["roll", "scrub", "engine", "all", "floor"]:
				var cell := _cell_new("3 surface, straight, %.0f m/s" % SURFACE_SPEED,
					"%s on %s" % [layer, SURFACE_NAMES[surface]], layer, SURFACE_SPEED)
				cell["surface"] = surface
				_cells.append(cell)

	if _cases.has("camera"):
		# Two halves and both are needed. With the listener pinned at the driver's
		# head the three camera positions should be indistinguishable, which is what
		# #160 bought and what nobody has checked in motion. With it cleared, the
		# listener falls back to whichever `Camera3D` is current -- ADR-0039 measured
		# 20.7 dB of swing from 1 m to 20 m with a reference sine, and this is the
		# same question asked of the whole moving mix.
		for pinned in [true, false]:
			for mode in ["chase", "cockpit", "free"]:
				var group := ("4 camera, listener at the driver's head" if pinned
					else "4 camera, listener CLEARED (pre-#160 fallback)")
				var cell := _cell_new(group, mode, "all", CAMERA_SPEED)
				cell["camera"] = mode
				cell["listener"] = pinned
				_cells.append(cell)

	if _cases.has("wav"):
		_wav_index = 0


# --- the driving loop ---------------------------------------------------------

func _physics_process(_delta: float) -> bool:
	_tick += 1
	if not _attached:
		if not _attach():
			quit(1)
			return true
		_attached = true
		_advance_cell()
		return false

	if _wav_index >= 0 and _cell >= _cells.size():
		return _wav_step()

	if _cell >= _cells.size():
		return _finish()

	_drive()
	_drain()
	_accumulate_operating_point()
	_phase_ticks += 1

	var cell: Dictionary = _cells[_cell]

	if _phase == "seek":
		if String(cell["layer"]) == "reference":
			_arm()
			return false
		var speed := _kart.get_speed_ms()
		if absf(speed - _target_speed) <= SEEK_TOLERANCE_MS:
			_seek_hits += 1
		else:
			_seek_hits = 0
		if _seek_hits >= SEEK_HOLD_TICKS or _phase_ticks >= SEEK_BUDGET_TICKS:
			# Recorded per cell rather than only in the aggregate check, because a
			# row measured at a speed the controller never reached is a row whose
			# level belongs to a different kart and the reader has to be able to see
			# which one it was.
			_cell_reached = _seek_hits >= SEEK_HOLD_TICKS
			_phase = "settle"
			_phase_ticks = 0
		return false

	if _phase == "settle":
		if _phase_ticks >= SETTLE_TICKS:
			_arm()
			if String(cell["event"]) == "shift":
				_shift_strikes_before = _strikes()
				# **Auto-shift has to come off first, and finding that out is a
				# result rather than plumbing.** `Drivetrain::apply_auto_shift`
				# overwrites `up` and `down` unconditionally whenever
				# `assists.auto_shift` is set, so `KartBody::request_shift_up`
				# latches a request that the solver then throws away. The first cut
				# of this cell reported "0 clacks struck" at all seven speeds with
				# the layer working perfectly.
				_kart.auto_shift = false
				# **Up first, then back down.** A downshift from a gear the kart is
				# already near the limiter in is refused by the gearbox, so opening
				# with one produced a window with no clack in it and a level row that
				# was really the mute floor. An upshift is always legal below top
				# gear and the downshift after it returns the ratio the controller was
				# holding the speed on.
				_kart.request_shift_up()
		return false

	# Measuring. The second shift lands a third of the way into the window so that
	# both clacks and the decay of the second one are inside it.
	if String(cell["event"]) == "shift" and _phase_ticks == int(MEASURE_TICKS / 3):
		_kart.request_shift_down()

	if _phase_ticks < MEASURE_TICKS:
		return false

	_record()
	_advance_cell()
	return false


## The controller and the mutes. One place, so a cell can never be measured with an
## input the report does not print.
func _drive() -> void:
	var speed := _kart.get_speed_ms()
	var error := _target_speed - speed
	if _forced_throttle >= 0.0:
		_throttle_now = _forced_throttle
		_brake_now = 0.0
		_speed_integral = 0.0
	else:
		_speed_integral = clampf(_speed_integral + error * INTEGRAL_GAIN / _tick_hz,
			-INTEGRAL_CLAMP, INTEGRAL_CLAMP)
		_throttle_now = clampf(error * THROTTLE_GAIN + _speed_integral, 0.0, 1.0)
		_brake_now = clampf(-error * BRAKE_GAIN - _speed_integral, 0.0, 1.0)
	# Ramped rather than stepped: a step to 0.35 is a snap input and a snap input is
	# a different transient from a corner.
	var step := CORNER_STEER / float(STEER_RAMP_TICKS)
	_steer_now = move_toward(_steer_now, _steer_target, step)
	_follow_camera()


## Put the probe camera where the active cell's mode puts it.
##
## **Every tick, and this is the line whose absence made the first camera table
## meaningless.** The cell applier set `current` and the listener state and nothing
## ever moved the node, so all six camera cells measured a camera sitting at the
## world origin several hundred meters behind a kart at z = +1000 -- three cells
## that were supposed to be 3.56 m, 0 m and 25 m away all read within 0.3 dB of each
## other, which reads exactly like "the camera does not matter" and was really "the
## camera never moved". The same family as CLAUDE.md's check-that-cannot-fail: the
## conclusion was available from the data and the data was measuring nothing.
func _follow_camera() -> void:
	if _camera_mode == "":
		return
	var target := _kart.global_position
	match _camera_mode:
		"chase":
			# `chase_camera.gd`'s own geometry: ARM_HEIGHT up and ARM_LENGTH back
			# along the kart's own axes, which is 3.56 m from the origin.
			_camera.global_position = (_kart.global_transform
				* Vector3(0.0, CHASE_ARM_HEIGHT, CHASE_ARM_LENGTH))
		"cockpit":
			_camera.global_position = _kart.to_global(_kart.driver_head_position())
		"free":
			_camera.global_position = target + Vector3(0.0, 2.0, FREE_DISTANCE)
	_camera.look_at_from_position(_camera.global_position, target
		+ Vector3(0.0, 0.001, 0.0), Vector3.UP)


func _advance_cell() -> void:
	_cell += 1
	if _cell >= _cells.size():
		if _wav_index >= 0:
			_wav_begin()
		return
	var cell: Dictionary = _cells[_cell]

	var previous_target := _target_speed
	var previous_steer := _steer_target
	var previous_surface := -1
	if _cell > 0:
		previous_surface = int(_cells[_cell - 1]["surface"])

	# **Between cells and never inside one.** The plane is 2,400 m square and spawn
	# is at z = +1000, so a run this long has to be put back at some point. Doing it
	# from the controller -- where the first cut did it -- fires in the middle of a
	# settle or a measure window: the 25 m/s group came out with its engine cell
	# measured at 3.83 m/s, its scrub cell at 15.42 and its wind cell at 23.26, three
	# different karts in one table, because every cell after the first skips the seek
	# when the target has not changed. Here it can only land on a boundary, and it
	# forces the seek that follows it.
	var respawned := false
	if _kart.global_position.distance_to(_kart.get_spawn().origin) > RESPAWN_DISTANCE_M:
		_kart.respawn()
		respawned = true

	_target_speed = float(cell["target"])
	_steer_target = float(cell["steer"])
	_forced_throttle = 0.0 if _break == "static" else -1.0
	_apply_surface(int(cell["surface"]))
	_apply_solo(String(cell["layer"]))
	_apply_camera(String(cell["camera"]), bool(cell["listener"]))

	# The reference cell plays the calibration sine and nothing else.
	var reference := String(cell["layer"]) == "reference"
	if reference and not _reference_player.playing:
		_reference_player.play()
	elif not reference and _reference_player.playing:
		_reference_player.stop()

	# Skip the seek when nothing that moves the kart changed. A surface change moves
	# the grip under it, so it counts.
	var unchanged := (is_equal_approx(previous_target, _target_speed)
		and is_equal_approx(previous_steer, _steer_target)
		and previous_surface == int(cell["surface"]) and _cell > 1 and not respawned)
	# The calibration cell has nothing to seek -- there is no target speed involved
	# in a reference sine -- but it does need the settle, because the reference
	# player was started this tick and a player that was just told to play has a
	# block or two of ramp in it.
	_phase = "settle" if (unchanged or reference) else "seek"
	_phase_ticks = 0
	_seek_hits = 0
	# A cell that inherits the previous one's speed also inherits whether that speed
	# was ever reached. A cell that seeks starts optimistic and the seek settles it.
	if not unchanged:
		_cell_reached = true
		# Cleared with the target, so a cell never inherits the wind-up of the one
		# before it.
		_speed_integral = 0.0


## Mute every player but one. See the header for why this is a volume and not a
## `stop()`.
func _apply_solo(layer: String) -> void:
	for key in _players.keys():
		var player: Node = _players[key]
		var audible: bool = layer == "all" or key == layer
		if _break == "solo":
			# The saboteur: nothing is ever muted, so every solo row is the whole
			# mix. Caught by `solo-isolates`, whose residual is the four layers
			# summed in power against the mix -- four copies of the mix is +6 dB and
			# is not a subtle failure to spot.
			audible = true
		if layer == "floor" or layer == "reference":
			audible = false
		if _break == "mute" and layer == "floor":
			# The saboteur for the floor check: the floor cell leaves the loudest
			# layer up, so the measured leakage floor is the engine note. Caught by
			# `mute-floor` and by nothing else.
			audible = key == "engine"
		player.volume_db = EngineVoiceRig.VOLUME_DB if audible else MUTE_DB


## Put the camera where a mode puts it, and pin or clear the listener.
##
## An empty mode means the cell does not care, and the camera is left wherever it
## was with the listener pinned -- which is the shipped configuration.
func _apply_camera(mode: String, pinned: bool) -> void:
	_camera_mode = mode
	if mode == "":
		if _listener != null and not _listener.is_current():
			_listener.make_current()
		return
	_camera.current = true
	_follow_camera()
	if pinned and _break != "listener":
		_listener.make_current()
	else:
		# `--break=listener` clears the listener in the cells that are supposed to
		# have it pinned, which is exactly the pre-#160 configuration. Caught by
		# `listener-pinned`, whose fingerprint is the spread across the three camera
		# positions -- a spread that is supposed to be under half a dB.
		_listener.clear_current()


## Set the ground collider's surface metadata.
##
## This is the real path and not a shortcut: `KartBody::query_ground` reads
## `surface_type` off whatever each suspension ray hit, so writing the meta is
## exactly what a grass collider does. It drives the surface integer rather than
## driving the kart onto a different piece of ground, which is what makes the
## question answerable without waiting on #241.
func _apply_surface(surface: int) -> void:
	if _ground == null:
		return
	if _break == "surface":
		# The saboteur: every cell is asphalt whatever it says it is. Caught by
		# `surface-responds`, whose fingerprint is the curb cell's rumble rate --
		# `v/lambda` is nonzero only on the curb, because it is the only surface in
		# `surface.h` with a ripple wavelength.
		surface = 0
	_ground.set_meta("surface_type", surface)


# --- measurement --------------------------------------------------------------

func _arm() -> void:
	_drain()
	_sum_left = 0.0
	_sum_right = 0.0
	_frames = 0
	_peak = 0.0
	_sum_x2 = 0.0
	_sum_dx2 = 0.0
	_prev_x = 0.0
	_op_ticks = 0
	_op_speed = 0.0
	_op_rpm = 0.0
	_op_slip_deg = 0.0
	_op_body_slip_deg = 0.0
	_op_speed_min = 1.0e9
	_op_speed_max = -1.0e9
	_phase = "measure"
	_phase_ticks = 0


## Pull everything the mixer produced since the last tick. Always called, in every
## phase, so the settle window's frames are discarded by being consumed rather than
## by being left to overflow the ring.
func _drain() -> void:
	var available := _capture.get_frames_available()
	if available <= 0:
		return
	var buffer := _capture.get_buffer(available)

	if _wav_index >= 0 and _cell >= _cells.size():
		for frame in buffer:
			# The RAW peak, before the writer's clamp. This is the number that says
			# how much gain reduction would clear the clipping, and it is invisible in
			# the finished file -- a 16-bit WAV cannot represent it, so a reader
			# measuring the WAV sees exactly 0.00 dBFS and cannot tell a mix that
			# grazes full scale from one that is 2 dB over it.
			_wav_peak = maxf(_wav_peak, maxf(absf(frame.x), absf(frame.y)))
			if absf(frame.x) >= 1.0 or absf(frame.y) >= 1.0:
				_wav_clipped += 1
			_wav_samples.append(frame.x)
			_wav_samples.append(frame.y)
		return

	if _phase != "measure":
		return
	for frame in buffer:
		_peak = maxf(_peak, maxf(absf(frame.x), absf(frame.y)))
		_sum_left += frame.x * frame.x
		_sum_right += frame.y * frame.y
		# The brightness index. Mono sum, because every positional source in this
		# mix is centered on the listener and the wind is written to both channels
		# identically, so L and R carry the same spectrum and averaging them only
		# lowers the variance.
		var mono := (frame.x + frame.y) * 0.5
		var diff := mono - _prev_x
		_sum_x2 += mono * mono
		_sum_dx2 += diff * diff
		_prev_x = mono
		_frames += 1


func _accumulate_operating_point() -> void:
	if _phase != "measure":
		return
	var telemetry := _kart.telemetry()
	var speed := float(telemetry["speed_ms"])
	_op_speed += speed
	_op_speed_min = minf(_op_speed_min, speed)
	_op_speed_max = maxf(_op_speed_max, speed)
	_op_rpm += float(telemetry["engine_rpm"])
	_op_gear = int(telemetry["gear"])

	# The mean of the contacting corners' slip angles, which is the aggregation
	# `publish_engine_audio` performs before normalizing it into the scrub drive.
	# Reported in degrees rather than normalized, so the row does not depend on this
	# file retyping `SCRUB_REFERENCE_SLIP_RAD`.
	var wheels: Array = telemetry["wheel"]
	var slip_sum := 0.0
	var slip_count := 0
	for entry in wheels:
		if bool(entry["tire_contact"]):
			slip_sum += absf(float(entry["slip_angle"]))
			slip_count += 1
	if slip_count > 0:
		_op_slip_deg += rad_to_deg(slip_sum / float(slip_count))

	# Body slip: the angle between where the kart points and where it is going.
	# #137's departure figure is quoted in these units, so a cell that has spun says
	# so in the same vocabulary as the ticket.
	var velocity := _kart.linear_velocity
	var planar := Vector2(velocity.x, velocity.z)
	if planar.length() > 0.5:
		var forward: Vector3 = -_kart.global_transform.basis.z
		var facing := Vector2(forward.x, forward.z)
		_op_body_slip_deg += rad_to_deg(absf(planar.angle_to(facing)))
	_op_ticks += 1


## Which `Camera3D` the viewport says is current, by name.
##
## Recorded per row rather than asserted, because "the camera the probe set" and
## "the camera the engine is using" are two different facts and this file has
## already been wrong about one of them once. With no `AudioListener3D` current the
## listener IS this node, so a camera table with no record of which camera was
## current is a table nobody can check.
func _current_camera_name() -> String:
	var viewport := get_root()
	if viewport == null:
		return "?"
	var camera := viewport.get_camera_3d()
	return "none" if camera == null else String(camera.name)


func _strikes() -> int:
	var stats: Dictionary = _streams["shift"].call("voice_stats")
	return int(stats["strikes"]) if stats.has("strikes") else -1


func _record() -> void:
	var cell: Dictionary = _cells[_cell]
	var count := float(maxi(_frames, 1))
	var mean_left := _sum_left / count
	var mean_right := _sum_right / count
	# Normalized so a full-scale sine present in both channels reads 0.00 dB: each
	# channel's mean square is then 0.5 and the sum is 1.0. The same convention
	# `audio_level_probe.gd` uses, so the two reports are comparable.
	var total := 10.0 * (log(maxf(mean_left + mean_right, 1.0e-18)) / log(10.0))

	var ticks := float(maxi(_op_ticks, 1))
	var row := {
		"group": String(cell["group"]),
		"label": String(cell["label"]),
		"layer": String(cell["layer"]),
		"target": float(cell["target"]),
		"surface": int(cell["surface"]),
		"camera": String(cell["camera"]),
		"listener": bool(cell["listener"]),
		"event": String(cell["event"]),
		"reached": _cell_reached,
		"listener_current": _listener != null and _listener.is_current(),
		"camera_current": _current_camera_name(),
		"left": _db(sqrt(mean_left)),
		"right": _db(sqrt(mean_right)),
		"total": total,
		"peak": _db(_peak),
		"peak_linear": _peak,
		"frames": _frames,
		"bright_hz": _brightness_hz(),
		"speed": _op_speed / ticks,
		"speed_min": _op_speed_min,
		"speed_max": _op_speed_max,
		"rpm": _op_rpm / ticks,
		"gear": _op_gear,
		"slip_deg": _op_slip_deg / ticks,
		"body_slip_deg": _op_body_slip_deg / ticks,
		"strikes": 0,
		"rumble_hz": 0.0,
	}

	if String(cell["event"]) == "shift":
		row["strikes"] = _strikes() - _shift_strikes_before
		# Back on for every following cell. The gear column in this table is the
		# automatic box's choice everywhere except inside an event window.
		_kart.auto_shift = true
	var roll_stats: Dictionary = _streams["roll"].call("voice_stats")
	if roll_stats.has("rumble_hz"):
		row["rumble_hz"] = float(roll_stats["rumble_hz"])

	if String(cell["layer"]) == "reference":
		_calibration_db = total
		_calibration_seen = true

	_rows.append(row)


## A brightness index, in Hz, and it is NOT a spectral centroid -- the name would
## be a lie and CLAUDE.md has an entry about numbers that hide what they are.
##
## It is the first-difference frequency estimate:
##
##     r    = mean((x[n] - x[n-1])^2) / mean(x[n]^2)
##     f    = (fs / pi) * asin(min(sqrt(r) / 2, 1))
##
## For a pure sine this is exact, because `mean(diff^2)/mean(x^2)` is
## `2(1 - cos(2*pi*f/fs))` = `(2*sin(pi*f/fs))^2`. For a broadband signal it
## returns the RMS frequency of the spectrum weighted by power, which moves
## monotonically with the centroid and is what makes it useful for comparing two
## surfaces. It is computed from the same window as the level, costs two
## multiply-adds per frame, and needs no FFT.
##
## The reason a real centroid is not computed: CLAUDE.md records that estimating a
## filter's response from one pass of noise put a 900 Hz band at 776 Hz and a Q of
## 2.4 at 10.2, because one DFT bin of one realization is Rayleigh-distributed. This
## estimator averages over every frame in the window instead of over one bin, so it
## has none of that variance -- at the cost of being a different quantity, which is
## why it has a different name.
func _brightness_hz() -> float:
	if _sum_x2 <= 0.0 or _frames < 2:
		return 0.0
	var ratio := _sum_dx2 / _sum_x2
	return (_mix_rate / PI) * asin(clampf(sqrt(ratio) * 0.5, 0.0, 1.0))


func _db(linear: float) -> float:
	return 20.0 * (log(maxf(linear, 1.0e-9)) / log(10.0))


# --- the WAV passes -----------------------------------------------------------

func _wav_begin() -> void:
	if _wav_index >= WAV_PASSES.size():
		return
	var pass_def: Array = WAV_PASSES[_wav_index]
	# **Stopped here and not only in `_advance_cell`.** The calibration cell is the
	# one cell that plays a full-scale sine, and `_advance_cell` stops it on the way
	# into the NEXT cell -- which never happens when the cell list is only the
	# calibration, as it is under `--case=wav`. Every WAV then carried a 0 dBFS
	# 500 Hz tone summed on top of the mix: raw peak +8.17 dBFS and 9.3% of samples
	# clipped, against 0.08% for the same pass in a full run. The files sounded
	# broken and the mix was fine.
	if _reference_player.playing:
		_reference_player.stop()
	_wav_samples = PackedFloat32Array()
	_wav_ticks = 0
	_wav_clipped = 0
	_wav_peak = 0.0
	_wav_target_ticks = int(float(pass_def[1]) * _tick_hz)
	_kart.respawn()
	_steer_now = 0.0
	_steer_target = 0.0
	_forced_throttle = -1.0
	_apply_surface(0)
	_apply_solo("all")
	_apply_camera("chase" if String(pass_def[0]) == "drive-no-listener" else "",
		String(pass_def[0]) != "drive-no-listener")
	match String(pass_def[0]):
		"drive", "drive-no-listener":
			_target_speed = 0.0
			_forced_throttle = 1.0
		"corner":
			_target_speed = 20.0
		"surfaces":
			_target_speed = SURFACE_SPEED
		"layers":
			_target_speed = 25.0


func _wav_step() -> bool:
	if _wav_index >= WAV_PASSES.size():
		return _finish()
	var key := String(WAV_PASSES[_wav_index][0])
	var seconds := float(_wav_ticks) / _tick_hz

	match key:
		"corner":
			# Two seconds of straight to settle the speed, then the steer ramps in.
			_steer_target = 0.0 if seconds < 2.0 else CORNER_STEER
		"surfaces":
			_apply_surface(clampi(int(seconds / 3.0), 0, 3))
		"layers":
			var order := ["engine", "scrub", "wind", "roll", "shift", "all"]
			var slot := clampi(int(seconds / 2.5), 0, order.size() - 1)
			_apply_solo(order[slot])
			# Clacks inside the shift slot, so the layer has something to say. The
			# auto-shift toggle is not optional -- `Drivetrain::apply_auto_shift`
			# discards a manual request while the assist is on, so with it left on
			# this slot is 2.5 s of the silence the layer correctly renders when no
			# shift has happened.
			_kart.auto_shift = order[slot] != "shift"
			if order[slot] == "shift":
				var into := seconds - 2.5 * float(slot)
				if into > 0.4 and into < 0.42:
					_kart.request_shift_up()
				if into > 1.4 and into < 1.42:
					_kart.request_shift_down()

	_drive()
	_drain()
	_wav_ticks += 1
	if _wav_ticks < _wav_target_ticks:
		return false

	_wav_write(key)
	_wav_index += 1
	if _wav_index >= WAV_PASSES.size():
		return _finish()
	_wav_begin()
	return false


## Write the captured frames as 16-bit stereo PCM.
##
## Written by hand rather than through a Godot resource because there is no
## `AudioStreamWAV` save path that takes a float buffer without going through an
## import, and because a RIFF header is twelve fields. The frames are exactly what
## the capture saw: downstream of the kart bus master, upstream of Master's trim,
## so the file is the shipped mix at the shipped level and not the trimmed one.
func _wav_write(key: String) -> void:
	if _out_dir == "":
		return
	var path := "%s/mix-%s.wav" % [_out_dir, key]
	var frames := _wav_samples.size() / 2
	var data := PackedByteArray()
	data.resize(frames * 4)
	for index in range(frames * 2):
		var value := clampf(_wav_samples[index], -1.0, 0.999969)
		var quantized := int(round(value * 32767.0))
		data.encode_s16(index * 2, quantized)

	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		_notes.append("could not write %s (%s)" % [path, error_string(FileAccess.get_open_error())])
		return
	var rate := int(round(_mix_rate))
	file.store_buffer("RIFF".to_ascii_buffer())
	file.store_32(36 + data.size())
	file.store_buffer("WAVE".to_ascii_buffer())
	file.store_buffer("fmt ".to_ascii_buffer())
	file.store_32(16)
	file.store_16(1)             # PCM
	file.store_16(2)             # stereo
	file.store_32(rate)
	file.store_32(rate * 4)      # byte rate
	file.store_16(4)             # block align
	file.store_16(16)            # bits per sample
	file.store_buffer("data".to_ascii_buffer())
	file.store_32(data.size())
	file.store_buffer(data)
	file.close()

	# The delivered-frames ratio. A capture drained on a physics tick can lose
	# frames if a tick runs long, and a WAV with holes in it sounds like a defect in
	# the mix rather than a defect in the instrument. Printed rather than corrected.
	var expected := float(_wav_target_ticks) / _tick_hz * _mix_rate
	_wav_written.append([key, path, frames, float(frames) / maxf(expected, 1.0),
		_wav_clipped, String(WAV_PASSES[_wav_index][2]), _db(_wav_peak)])


# --- checks and the report ----------------------------------------------------

func _check(name: String, ok: bool, detail: String) -> void:
	_checks.append(["ok  " if ok else "FAIL", name, detail])
	if not ok:
		_failures.append(name)


## A measured number that the report states and the exit code ignores.
##
## Marked `note` rather than being folded into the prose, so a reader scanning the
## checks block sees it in the same place as the assertions and can tell at a glance
## which numbers this gate is willing to fail on. Everything about the BALANCE of
## the mix ends up here: `engine_voice_rig.gd` says each of its gains is "estimated
## and owed an ear", and a gate that asserted one would have made the judgement.
func _report_only(name: String, detail: String) -> void:
	_checks.append(["note", name, detail])


func _rows_in(group_prefix: String) -> Array:
	var out: Array = []
	for row in _rows:
		if String(row["group"]).begins_with(group_prefix):
			out.append(row)
	return out


## The first steady row matching a group, layer and operating point.
##
## **Event rows are skipped**, because the shift layer has two cells at every speed
## -- a steady one that carries the clutch and an event one that carries two clacks
## -- and a decomposition that picked up the transient window would be summing a
## level nothing in the mix holds.
func _find_row(group_prefix: String, layer: String, target: float, surface := -1,
		camera := "") -> Dictionary:
	for row in _rows:
		if not String(row["group"]).begins_with(group_prefix):
			continue
		if String(row["layer"]) != layer or String(row["event"]) != "":
			continue
		if target >= 0.0 and not is_equal_approx(float(row["target"]), target):
			continue
		if surface >= 0 and int(row["surface"]) != surface:
			continue
		if camera != "" and String(row["camera"]) != camera:
			continue
		return row
	return {}


## Is this row indistinguishable from its own group's all-muted floor?
##
## Matched on group, target speed and surface, so a floor measured at 20 m/s on
## grass is never used to judge a row measured at 35 m/s on asphalt. Returns false
## when the group has no floor cell, because "no floor was measured" and "the layer
## is audible" are different statements and only one of them is a claim.
func _is_silent(row: Dictionary) -> bool:
	var over := _over_floor_db(row)
	return over != NO_FLOOR and absf(over) <= SILENT_BAND_DB


## How far this row sits above its own group's all-muted floor, dB, or -1 when the
## group has no floor cell.
##
## Matched on group, target speed and surface, so a floor measured at 20 m/s on
## grass is never used to judge a row measured at 35 m/s on asphalt. "No floor was
## measured" and "the layer is audible" are different statements and only one of
## them is a claim, which is why the miss returns a sentinel rather than a big
## number.
func _over_floor_db(row: Dictionary) -> float:
	if String(row["layer"]) in ["floor", "reference", "all"]:
		return NO_FLOOR
	for candidate in _rows:
		if String(candidate["layer"]) != "floor":
			continue
		if String(candidate["group"]) != String(row["group"]):
			continue
		if not is_equal_approx(float(candidate["target"]), float(row["target"])):
			continue
		if int(candidate["surface"]) != int(row["surface"]):
			continue
		return float(row["total"]) - float(candidate["total"])
	return NO_FLOOR


func _power_sum_db(values: Array) -> float:
	var total := 0.0
	for db in values:
		total += pow(10.0, float(db) / 10.0)
	return 10.0 * (log(maxf(total, 1.0e-18)) / log(10.0))


func _run_checks() -> void:
	# 0. The capture path. Everything below is a difference against this, so a
	# capture wrong by a factor has to be excluded before any of it means anything.
	_check("capture-honest",
		_calibration_seen and absf(_calibration_db) <= 0.10,
		"a full-scale sine on the probe bus reads %+.2f dBFS (must be 0.00 +/- 0.10)"
			% _calibration_db)

	if _cases.has("level"):
		var worst_ratio := 1.0e9
		var worst_label := ""
		for row in _rows_in("1 "):
			var target := float(row["target"])
			if target <= 0.0:
				continue
			var ratio := float(row["speed"]) / target
			if ratio < worst_ratio:
				worst_ratio = ratio
				worst_label = String(row["label"])
		_check("kart-moves", worst_ratio >= SPEED_REACHED_FRACTION,
			"worst cell reached %.1f%% of its target (%s); the floor is %.0f%%"
				% [worst_ratio * 100.0, worst_label, SPEED_REACHED_FRACTION * 100.0])

		# The decomposition. Four independent generators summed in power must equal
		# the mix; if they do not, either the mute is leaking or a layer is not
		# reaching the bus, and both are defects in the table rather than in the kart.
		var worst_residual := 0.0
		var residual_label := ""
		for speed in _speeds:
			var parts: Array = []
			for layer in ["engine", "scrub", "wind", "roll", "shift"]:
				var row := _find_row("1 ", layer, speed)
				if row.is_empty():
					continue
				parts.append(float(row["total"]))
			var mix := _find_row("1 ", "all", speed)
			if mix.is_empty() or parts.size() < 5:
				continue
			var residual := _power_sum_db(parts) - float(mix["total"])
			if absf(residual) > absf(worst_residual):
				worst_residual = residual
				residual_label = "%.0f m/s" % speed
		_check("solo-isolates", absf(worst_residual) <= 1.5,
			"the five steady layers summed in power differ from the mix by at most "
			+ "%+.2f dB (%s); the limit is 1.50" % [worst_residual, residual_label])

		# **The check is on the MUTE, not on the layers.**
		#
		# The first cut asserted that every layer sits clear of the all-muted floor,
		# and that is a demand the mix has no obligation to meet: the scrub layer is
		# legitimately silent in a straight line, and at 25 m/s it sits 3.1 dB up
		# because there is a hair of slip in a kart running straight. Both are the
		# layer being correct, and both turned that check red. A check that fires on
		# correct behavior is worse than no check -- somebody eventually widens it
		# until it cannot fail.
		#
		# What actually has to be true for the per-layer table to mean anything is
		# that the mute is DEEP: the floor has to sit far enough below the mix that
		# no muted layer can contribute measurably to a row. That is one number per
		# group, it is a property of the instrument rather than of the kart, and
		# `--break=mute` moves it by 70 dB.
		var worst_depth := 1.0e9
		var depth_where := ""
		for speed in _speeds:
			var floor_row := _find_row("1 ", "floor", speed)
			var mix := _find_row("1 ", "all", speed)
			if floor_row.is_empty() or mix.is_empty():
				continue
			var depth: float = float(mix["total"]) - float(floor_row["total"])
			if depth < worst_depth:
				worst_depth = depth
				depth_where = "%.0f m/s" % speed
		_check("mute-floor", worst_depth >= MUTE_DEPTH_DB,
			"the all-muted floor sits %.1f dB below the full mix at worst (%s); the "
			% [worst_depth, depth_where]
			+ "floor is %.0f dB, at which a muted layer is %.4f%% of a row's power"
				% [MUTE_DEPTH_DB, pow(10.0, -MUTE_DEPTH_DB / 10.0) * 100.0])

		# Headroom, and the loudest the whole mix gets. Six layers summing is where a
		# mix runs out of headroom, and nobody had looked at the peak of a moving
		# kart at all.
		var worst_peak := -200.0
		var peak_label := ""
		var loudest_total := -200.0
		var loudest_label := ""
		for row in _rows:
			if String(row["layer"]) != "all":
				continue
			if float(row["peak"]) > worst_peak:
				worst_peak = float(row["peak"])
				peak_label = String(row["label"])
			if float(row["total"]) > loudest_total:
				loudest_total = float(row["total"])
				loudest_label = String(row["label"])

		# **Reported and NOT asserted, deliberately.** Headroom is set by
		# `MASTER_GAIN_DB`, which is a level decision somebody made with an argument
		# attached, and this wave's rule is that a constant somebody dislikes is a
		# proposal with numbers rather than an edit. A gate that failed on it would
		# be that edit by another name -- it would force the constant to move to get
		# back to green. So the number is printed as loudly as a failure would be and
		# the decision stays with the person who owns the mix.
		_report_only("headroom",
			"the loudest mix PEAK is %.2f dBFS (%s). Full scale is 0.00; anything at "
				% [worst_peak, peak_label]
			+ "or above it is clipped by the device.")

		# What IS asserted about level: a sanity band, not a judgement. -40 dBFS is
		# 1% of full scale and is below any usable playback level; 0 dBFS is full
		# scale. Neither endpoint is where a mix should sit -- they are where it is
		# definitely broken, and the whole 40 dB in between is somebody else's call.
		# What the event cells found out about the input path, reported because it is
		# not a fact about the mix and this gate must not start asserting gearbox
		# behavior. `Drivetrain::apply_auto_shift` overwrites the manual `up`/`down`
		# request unconditionally while `assists.auto_shift` is set, so every
		# `request_shift_up` from a probe -- or from the E key, or from Square -- is
		# latched by `KartBody` and then discarded by the solver. The probe turns
		# auto-shift off for the length of an event window to get a clack at all.
		var clacks := 0
		for row in _rows:
			if String(row["event"]) == "shift":
				clacks += int(row["strikes"])
		_report_only("manual-shift",
			"%d clacks struck across %d event windows, with auto-shift forced OFF for "
				% [clacks, _speeds.size()]
			+ "the length of each. With it ON the solver discards the request and the "
			+ "layer is correctly silent -- see Drivetrain::apply_auto_shift.")

		_check("mix-level", loudest_total > -40.0 and loudest_total < 0.0,
			"the loudest mix RMS is %.2f dBFS (%s); the sanity band is -40.00 to 0.00"
				% [loudest_total, loudest_label])

	if _cases.has("camera"):
		var pinned: Array = []
		for mode in ["chase", "cockpit", "free"]:
			var row := _find_row("4 camera, listener at", "all", CAMERA_SPEED, -1, mode)
			if not row.is_empty():
				pinned.append(float(row["total"]))
		var spread := 0.0
		if pinned.size() >= 2:
			spread = pinned.max() - pinned.min()
		_check("listener-pinned", pinned.size() >= 2 and spread <= CAMERA_SPREAD_LIMIT_DB,
			"chase / cockpit / free differ by %.2f dB with the listener pinned; "
			% spread + "the limit is %.2f" % CAMERA_SPREAD_LIMIT_DB)

	if _cases.has("surface"):
		var levels: Array = []
		var curb_rumble := 0.0
		var curb_speed := 0.0
		var asphalt_rumble := -1.0
		for surface in range(SURFACE_NAMES.size()):
			var row := _find_row("3 ", "roll", SURFACE_SPEED, surface)
			if row.is_empty():
				continue
			levels.append(float(row["total"]))
			if surface == 1:
				curb_rumble = float(row["rumble_hz"])
				curb_speed = float(row["speed"])
			if surface == 0:
				asphalt_rumble = float(row["rumble_hz"])
		var spread := 0.0
		if levels.size() >= 2:
			spread = levels.max() - levels.min()
		# Two witnesses, and the second is the one a sabotage cannot fake. A level
		# spread could be produced by the speed drifting between cells; a rumble rate
		# equal to `v/lambda` can only be produced by `SURFACE_CURB` reaching
		# `RollSynth`, because the curb is the only row in `surface.h` with a ripple
		# wavelength at all and every other surface must publish a flat zero.
		#
		# **Against the ACHIEVED speed and not the target.** The controller holds
		# 19.43 m/s where it was asked for 20.0, and 19.43/0.15 is 129.5 Hz against
		# the 133.3 the target implies -- so comparing against the target would have
		# read as a 2.9% error in the synth and was really the probe quoting a speed
		# the kart was not doing.
		var expected_hz := curb_speed / CURB_WAVELENGTH_M
		var rumble_error := 1.0
		if expected_hz > 0.0:
			rumble_error = absf(curb_rumble - expected_hz) / expected_hz
		var rumble_ok := rumble_error <= 0.03 and asphalt_rumble == 0.0
		_check("surface-responds",
			levels.size() == SURFACE_NAMES.size() and spread >= SURFACE_SPREAD_LIMIT_DB
				and rumble_ok,
			"rolling layer spreads %.2f dB across the four surfaces (floor %.2f); "
			% [spread, SURFACE_SPREAD_LIMIT_DB]
			+ "curb rumble %.1f Hz against %.1f Hz from v/lambda at the achieved "
				% [curb_rumble, expected_hz]
			+ "%.2f m/s (%.2f%% off, limit 3%%), asphalt %.1f Hz"
				% [curb_speed, rumble_error * 100.0, asphalt_rumble])


func _finish() -> bool:
	_run_checks()
	_report()
	if _break != "":
		var expected := String(BREAK_EXPECT[_break])
		if _failures.has(expected):
			print("    CAUGHT   --break=%s turned '%s' red, which is its own fingerprint."
				% [_break, expected])
			print("")
			quit(0)
			return true
		if _failures.size() > 0:
			print("    WRONG RED  --break=%s left '%s' green and broke %s instead."
				% [_break, expected, ", ".join(_failures)])
			print("")
			quit(1)
			return true
		print("    MISSED   --break=%s changed nothing. A check that cannot fail is not a check."
			% _break)
		print("")
		quit(1)
		return true
	quit(1 if _failures.size() > 0 else 0)
	return true


func _report() -> void:
	var lines: Array[String] = []
	lines.append("")
	lines.append("=== the audio mix of a MOVING kart. #242 area 4 ===")
	lines.append("")
	lines.append("    audio driver            %s" % AudioServer.get_driver_name())
	lines.append("    mix rate                %.1f Hz" % _mix_rate)
	lines.append("    physics tick rate       %.1f Hz" % _tick_hz)
	lines.append("    scene                   %s" % SCENE_PATH)
	lines.append("    ground                  flat asphalt plane, 2400 m square, no "
		+ "elevation and no camber")
	lines.append("    speed control           proportional, target vs achieved printed "
		+ "per row")
	lines.append("    assists                 auto-shift %s, auto-clutch %s   (set by the "
		% [_kart.auto_shift, _kart.auto_clutch]
		+ "probe, not read from user://)")
	lines.append("    window                  %.2f s measured after %.2f s settle"
		% [MEASURE_TICKS / _tick_hz, SETTLE_TICKS / _tick_hz])
	lines.append("    layer separation        volume_db %.0f on every other player; "
		% MUTE_DB + "the `floor` row is all six muted")
	lines.append("    kart bus master         %.2f dB   (EngineVoiceRig.MASTER_GAIN_DB)"
		% AudioServer.get_bus_volume_db(AudioServer.get_bus_index(EngineVoiceRig.KART_BUS)))
	lines.append("    Master trim             %.1f dB   (downstream of the capture; "
		% AudioServer.get_bus_volume_db(AudioServer.get_bus_index("Master"))
		+ "moves nothing below)")
	lines.append("    calibration             %+.2f dBFS for a full-scale sine" % _calibration_db)
	lines.append("    cases                   %s" % ", ".join(_cases))
	if _break != "":
		lines.append("    SABOTAGE                --break=%s, must turn '%s' red"
			% [_break, String(BREAK_EXPECT[_break])])
	lines.append("")
	lines.append("    positional layers: engine (unit 3.0), scrub (2.0), shift (3.0), "
		+ "roll (2.0).")
	lines.append("    NON-positional: wind. ADR-0039 measured a positional player "
		+ "3.19 dB down in")
	lines.append("    total power against a plain one at the same gain, so the wind row "
		+ "carries")
	lines.append("    3.19 dB the other four do not. It is NOT subtracted anywhere below.")
	lines.append("")
	lines.append("    dBFS. `total` is both channels summed in power and reads 0.00 for a")
	lines.append("    full-scale sine in both. `bright` is a first-difference frequency")
	lines.append("    estimate and is NOT a spectral centroid -- see `_brightness_hz`.")

	var group := ""
	for row in _rows:
		if String(row["group"]) != group:
			group = String(row["group"])
			lines.append("")
			lines.append("--- %s" % group)
			lines.append("")
			lines.append("    cell                     total     peak    bright   "
				+ "speed  target    rpm  gr  slip  bslip   extra")
		var extra := ""
		# A row within `SILENT_BAND_DB` of its own group's all-muted floor is not a
		# quiet layer, it is a silent one, and quoting a level for it would be
		# quoting the leakage of the layers that were muted. Marked rather than
		# suppressed, so the reader can see which cells the synth had nothing to say
		# in -- scrub in a straight line is the whole of #84's layer being correctly
		# absent, not a defect.
		var over_floor := _over_floor_db(row)
		if _is_silent(row):
			extra = "SILENT (at the mute floor)"
		elif over_floor != NO_FLOOR and over_floor < FLOOR_MARGIN_DB:
			extra = "only %.1f dB over the mute floor" % over_floor
		if String(row["event"]) == "shift":
			extra = "%s %d clacks struck" % [extra, int(row["strikes"])]
		if float(row["rumble_hz"]) > 0.0 and String(row["layer"]) in ["roll", "all"]:
			extra = "%s rumble %.1f Hz" % [extra, float(row["rumble_hz"])]
		if not bool(row["reached"]):
			extra = "%s TARGET NOT REACHED" % extra
		if String(row["camera"]) != "":
			extra = "%s listener=%s camera=%s" % [extra,
				"head" if bool(row["listener_current"]) else "CLEARED",
				String(row["camera_current"])]
		lines.append("    %-22s %7.2f  %7.2f  %7.0f  %6.2f  %6.1f  %5.0f  %2d  %4.1f  %5.1f   %s"
			% [String(row["label"]), float(row["total"]), float(row["peak"]),
				float(row["bright_hz"]), float(row["speed"]), float(row["target"]),
				float(row["rpm"]), int(row["gear"]), float(row["slip_deg"]),
				float(row["body_slip_deg"]), extra])

	_report_balance(lines)

	if _wav_written.size() > 0:
		lines.append("")
		lines.append("--- WAVs, in the order they were rendered")
		lines.append("")
		for entry in _wav_written:
			lines.append("    %s" % String(entry[1]))
			lines.append("        %d frames, %.3f of the frames the pass should have "
				% [int(entry[2]), float(entry[3])]
				+ "produced. RAW peak %+.2f dBFS before the writer's clamp, "
					% float(entry[6])
				+ "%d clipped samples (%.3f%%)"
					% [int(entry[4]), 100.0 * float(entry[4]) / maxf(float(entry[2]) * 2.0, 1.0)])
			lines.append("        %s" % String(entry[5]))

	lines.append("")
	lines.append("--- checks")
	lines.append("")
	for entry in _checks:
		lines.append("    %s %-20s %s" % [entry[0], entry[1], entry[2]])
	lines.append("")
	lines.append("    %d checked, %d failed" % [_checks.size(), _failures.size()])
	for note in _notes:
		lines.append("    note: %s" % note)
	lines.append("")

	for line in lines:
		print(line)


## Where the balance actually moves, which is the question nobody had a number for.
##
## Reported as differences against the engine note at the same speed, plus the
## interpolated speed at which each layer crosses it. **Interpolated between the
## bracketing rows and not snapped to the first row past the crossing** -- CLAUDE.md
## records that snapping to the first probe past a -3 dB point biased a bandwidth
## 15% wide on a 5% grid, and a 5 m/s grid is coarser than that.
func _report_balance(lines: Array[String]) -> void:
	if not _cases.has("level"):
		return
	lines.append("")
	lines.append("--- balance against the engine note, straight, asphalt")
	lines.append("")
	lines.append("    speed      engine    scrub-eng   wind-eng   roll-eng    mix-eng")
	var series := {"scrub": [], "wind": [], "roll": []}
	var silent_count := {"scrub": 0, "wind": 0, "roll": 0}
	var measured_count := {"scrub": 0, "wind": 0, "roll": 0}
	for speed in _speeds:
		var engine := _find_row("1 ", "engine", speed)
		if engine.is_empty():
			continue
		var engine_db := float(engine["total"])
		var cells: Array = []
		for layer in ["scrub", "wind", "roll"]:
			var row := _find_row("1 ", layer, speed)
			var delta := (float(row["total"]) - engine_db) if not row.is_empty() else NAN
			var mark := " "
			if not row.is_empty():
				measured_count[layer] += 1
				series[layer].append([speed, delta])
				if _is_silent(row):
					silent_count[layer] += 1
					mark = "*"
			cells.append("%9.2f%s" % [delta, mark])
		var mix := _find_row("1 ", "all", speed)
		var mix_delta := (float(mix["total"]) - engine_db) if not mix.is_empty() else NAN
		lines.append("    %5.1f m/s %8.2f  %s %s %s %10.2f"
			% [speed, engine_db, cells[0], cells[1], cells[2], mix_delta])
	lines.append("")
	lines.append("    * the layer is at the all-muted floor in that cell, so the number is")
	lines.append("      leakage rather than a level. It is printed, not suppressed, because")
	lines.append("      `scrub is silent in a straight line` is a fact about the mix.")

	lines.append("")
	for layer in ["scrub", "wind", "roll"]:
		var points: Array = series[layer]
		if measured_count[layer] > 0 and silent_count[layer] == measured_count[layer]:
			lines.append("    %s is SILENT at every speed in this protocol -- it never rises "
				% layer + "out of the mute floor")
			continue
		var crossing := _crossing_speed(points)
		if crossing > 0.0:
			lines.append("    %s overtakes the engine note at %.1f m/s (%.0f km/h), "
				% [layer, crossing, crossing * 3.6]
				+ "interpolated between the bracketing rows")
		else:
			var last: Array = points[-1] if points.size() > 0 else [0.0, NAN]
			lines.append("    %s never overtakes the engine note inside %.0f m/s "
				% [layer, _speeds[-1]]
				+ "-- it is still %.1f dB under at the top row" % -float(last[1]))


## Linear interpolation of the speed at which a delta series crosses zero.
func _crossing_speed(points: Array) -> float:
	for index in range(1, points.size()):
		var previous: Array = points[index - 1]
		var current: Array = points[index]
		var a := float(previous[1])
		var b := float(current[1])
		if a < 0.0 and b >= 0.0:
			var span := b - a
			if absf(span) < 1.0e-9:
				return float(current[0])
			return float(previous[0]) + (float(current[0]) - float(previous[0])) * (-a / span)
	return -1.0
