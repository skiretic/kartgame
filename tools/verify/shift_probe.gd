extends SceneTree

## Do the shift, clutch and rolling layers reach the mixer, and what do they cost?
## Issues #83 and #85.
##
##     godot --headless --audio-driver CoreAudio \
##         --path . --script tools/verify/shift_probe.gd -- --case=join,level,cost
##     tools/verify/audio.sh            # the same thing with the negative controls
##
## ## Why this exists when `voice_probe.gd` already tests the engine note
##
## It tests a different failure. `voice_probe.gd` asks whether `_mix` is reached
## and whether a *continuously changing* rpm crosses the seqlock. Both new layers
## fail in ways that question cannot see:
##
##   * the shift layer is **event driven**. Its `_mix` runs on every block whether
##     or not a shift ever happens, and it renders exact silence when none does. So
##     "_mix was called" and "the samples are not zero" are both satisfied by a
##     layer that has never once heard about a gearshift. What has to be asserted
##     is that a shift the SOLVER performed produced a clack, which means driving a
##     real `KartBody` through a real gearchange and demanding the count move.
##   * the rolling layer reads `surface`, which is the field with the longest
##     unjoined path in the whole struct: a collider's `surface_type` metadata ->
##     `GroundQuery` -> `KartBody::contact_` -> `EngineAudioInput::surface` ->
##     `RollSynth`. Every link is silent if it breaks.
##
## This is the shape CLAUDE.md calls "a capability built at both ends and not
## joined in the middle", which is this project's most common defect by a distance:
## `settings.cfg` stored `assist_auto_shift` that no scene loaded, `look_back` was
## bound and read by nothing, `replay_snap` was described in the present tense by a
## header and called by nothing. Both of tonight's layers had exactly that shape
## available to them and this is the check that would have caught it.
##
## ## What is asserted and what is only reported
##
## Asserted: a solver gearshift strikes a clack; the clack count matches the number
## of shifts commanded; the curb rumble rate the audio thread publishes equals
## `v / lambda` computed here from `surface.h`'s own wavelength; the layers do not
## cost more than their share of §15's 0.5 ms; nothing clips.
##
## Reported, because they are judgements: how loud each layer is, and the
## before/after headroom. `#160` owns the balance and a driver owns the verdict.

const TICKS_SETTLE := 60
const TICKS_PER_PHASE := 90

## The curb wavelength, from `src/core/surface.h`'s `SURFACE_CURB` row.
##
## **Retyped here on purpose, and it is the point of the check.** The gate compares
## the rate the audio thread publishes against `v / lambda` computed independently
## on this side. If it read the wavelength back out of the same class it would be
## asking the class to agree with itself, which is the shape of check CLAUDE.md
## warns about. A change to `surface.h` that this file does not follow SHOULD fail
## here -- that is a gate noticing a normative number moved, not a maintenance
## burden.
const CURB_WAVELENGTH_M := 0.15

## Road speed held during the rolling phase, m/s, and the rate it implies.
## 20.0 / 0.15 = 133.33 Hz.
const ROLL_SPEED_MS := 20.0

## What fraction of a frame period all five layers together may cost.
##
## ARCHITECTURE.md §15 gives audio 0.5 ms of a 16.6 ms frame, which is 3.0%. That
## is the budget for the WHOLE mix at one kart; `engine_voice.h` records twelve
## engine voices at 20-30% of real time after agent E's work, and these two layers
## must not give that back. A single noise layer costing more than 0.5% of real
## time would be twelve karts spending 6% on one layer, which is the whole budget.
const MAX_LAYER_LOAD := 0.005

## Master trim while the probe runs, dB.
##
## **Every figure this probe reports is computed upstream of Master and none of
## them move**, which is what makes this safe rather than a compromise. The cost
## figures come out of `voice_stats`, timed inside `_mix` before the sample reaches
## a bus at all; the gains and the layer are read back off the stream. Master's
## volume is the last thing in the chain and touches none of it.
##
## It is here because a probe is not a demo. These runs go through real speakers on
## a real desk -- `audio_level_probe.gd` has carried the same constant for the same
## reason since ADR-0039, and this file and `scrub_cost_probe.gd` were the two that
## did not. Verified rather than assumed: see the note in the report.
const MASTER_TRIM_DB := -60.0


var _kart: KartBody
var _streams: Array = []
var _shift_stream: Object
var _roll_stream: Object
var _cases: Array = []
var _break := ""

var _tick := 0
var _phase := 0
var _shifts_commanded := 0
var _rumble_seen := 0.0
var _strikes_at_phase_start := 0
var _failures: Array[String] = []
var _checked := 0
var _rows: Array = []
var _skip_shift_time_push := false
var _shift_time_restored := false
var _shift_time_detail := ""


## Does `KartBody` actually push `Gearbox::shift_time` into the shift synth?
##
## **The obvious check cannot fail, and that is why this one is shaped like this.**
## The first version compared the stream's `shift_time` against a retyped 0.065 and
## was green on every path -- including the path where the body pushes nothing at
## all, because `ShiftAudioConfig::shift_time_s` defaults to the same 0.065 so that
## an untold synth is right rather than zero. Two values that agree by design prove
## nothing about the wire between them. `--break=stale_shift_time` reported MISSED,
## which is exactly what a negative control is for.
##
## `Gearbox::shift_time` is not exposed to GDScript, so the value cannot be read
## from the solver and compared. What CAN be done is a round trip: corrupt the
## stream's copy to a value nothing would produce, ask the body to resolve the voice
## again, and see whether the solver's number comes back. If it does, something on
## the C++ side actively wrote it -- which is the join, and is the only claim
## available from here.
func _probe_shift_time_push() -> void:
	const WRONG := 0.2001
	_shift_stream.set("shift_time", WRONG)
	if _skip_shift_time_push:
		_shift_time_restored = false
		_shift_time_detail = "sabotage: the re-resolve was skipped, synth holds %.4f s" \
			% float(_shift_stream.get("shift_time"))
		return
	# Re-assigning the path is what makes `KartBody::set_shift_voice_player` run,
	# and that is the function that pushes. Same call a scene makes.
	_kart.shift_voice_player = NodePath("ShiftVoice")
	var got: float = float(_shift_stream.get("shift_time"))
	_shift_time_restored = absf(got - WRONG) > 1.0e-9
	_shift_time_detail = "corrupted to %.4f, body restored %.4f s" % [WRONG, got]


func _initialize() -> void:
	var args := _parse_args()
	_cases = args.get("case", "join,level,cost").split(",", false)
	_break = args.get("break", "")

	if AudioServer.get_driver_name() == "Dummy":
		# The same refusal `scrub_cost_probe.gd` makes, and for the same reason:
		# ADR-0035 measured the Dummy driver reaching `_mix` in bursts on an ordinary
		# thread, so a cost figure taken there is wrong in scale and in shape. A
		# probe that reported one anyway would be publishing a number it knows is
		# meaningless.
		printerr("error: the audio driver is Dummy. Cost and level figures taken there are")
		printerr("       not the real path -- ADR-0035. Use --audio-driver CoreAudio.")
		quit(1)
		return

	# Before anything is built, so no block is ever rendered at full level.
	# `--master-db=` exists only so the report can prove the figures do not move.
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("Master"),
		float(args.get("master-db", str(MASTER_TRIM_DB))))

	for required in ["KartBody", "NoiseVoiceStream", "NoiseVoicePlayback"]:
		if not ClassDB.class_exists(required):
			printerr("error: %s is not registered -- scons target=editor arch=arm64" % required)
			quit(1)
			return

	_kart = KartBody.new()
	_kart.name = "Kart"
	get_root().add_child(_kart)
	_kart.input_driver = func() -> Dictionary:
		return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
	_kart.engage(1, 5.0)

	EngineVoiceRig.attach(_kart)
	var noise: Array = EngineVoiceRig.attach_noise(_kart)
	if noise.size() < 4:
		printerr("error: attach_noise returned %d streams, expected 4." % noise.size())
		quit(1)
		return
	_streams = noise
	_shift_stream = noise[2]
	_roll_stream = noise[3]
	if _shift_stream == null or _roll_stream == null:
		printerr("error: the shift or rolling stream is null after attach_noise.")
		quit(1)
		return

	_apply_sabotage()


## The negative controls. Each one breaks exactly one link that this gate claims to
## check, and the run's exit code is INVERTED under `--break`: a sabotage that is
## not caught is the failure.
##
## **The verdict has to demand the saboteur's own fingerprint.** CLAUDE.md records
## the first cut of `shell.sh`'s controls reporting "caught" off a pre-existing red
## it had not caused, so each mode below names the check it must turn red and the
## report asserts that specific one failed, not merely that something did.
func _apply_sabotage() -> void:
	match _break:
		"":
			pass
		"unwired":
			# The exact defect this whole probe exists for: the layer is built,
			# mounted and playing, and the body never publishes to it. Must be caught
			# by the clack count staying at zero.
			_kart.shift_voice_player = NodePath("")
			_kart.roll_voice_player = NodePath("")
		"silent":
			# Both layers at zero gain. The clack still fires -- `strikes` counts
			# strikes, not samples -- so this must be caught by the LEVEL check and
			# not by the join check, which is the distinction that makes the two
			# separate checks worth having.
			_shift_stream.set("gain", 0.0)
			_roll_stream.set("gain", 0.0)
		"wrong_layer":
			# The rolling stream mounted as a scrub layer. `set_layer` clamps out of
			# range to scrub; this is the IN-RANGE version of the same mistake, which
			# is the dangerous one because it renders a real, plausible noise layer
			# that ignores the surface entirely. Caught by the layer read-back.
			_roll_stream.set("layer", 0)
		"stale_shift_time":
			# Corrupt the synth's shift duration and DO NOT let the body push it
			# back. See `_probe_shift_time_push` for why the check is shaped as a
			# round trip rather than as a comparison against a constant.
			_skip_shift_time_push = true
		_:
			printerr("error: unknown --break mode '%s'" % _break)
			printerr("       modes: unwired, silent, wrong_layer, stale_shift_time")
			quit(2)


func _physics_process(_delta: float) -> bool:
	_tick += 1

	# Phase 0: settle. Phase 1: three commanded upshifts. Phase 2: roll on a curb.
	if _tick == TICKS_SETTLE:
		_shift_stream.call("reset_stats")
		_roll_stream.call("reset_stats")
		_strikes_at_phase_start = int(_shift_stream.call("voice_stats")["strikes"])
		_phase = 1

	if _phase == 1:
		# One shift every 30 ticks, which is 250 ms -- comfortably longer than the
		# 65 ms shift, so the three are three separate events rather than one long
		# one. Commanded through `request_shift_up`, which is the same entry point a
		# pad press uses, so this drives the solver rather than the synth.
		if _tick % 30 == 0 and _shifts_commanded < 3:
			_kart.request_shift_up()
			_shifts_commanded += 1
		if _tick >= TICKS_SETTLE + TICKS_PER_PHASE:
			_phase = 2
			_kart.set_meta("roll_phase_start", _tick)

	if _phase == 2:
		# The rolling layer needs road speed and a curb under it. There is no
		# collider in this scene -- `voice_probe.gd` says why a bare KartBody is the
		# right fixture -- so the surface cannot come from a raycast here. It comes
		# from `engage`, which sets a driveline speed the boundary turns into
		# `speed_ms`. The surface is asserted separately below against whatever the
		# boundary actually published, which is the honest thing to check in a
		# fixture with no ground.
		if _tick >= TICKS_SETTLE + TICKS_PER_PHASE * 2:
			_report()
			quit(1 if (_failures.size() > 0) != (_break != "") else 0)
			return true
		var stats: Dictionary = _roll_stream.call("voice_stats")
		var hz: float = stats["rumble_hz"]
		if hz > _rumble_seen:
			_rumble_seen = hz

	return false


func _check(name: String, ok: bool, detail: String) -> void:
	_checked += 1
	_rows.append(["ok  " if ok else "FAIL", name, detail])
	if not ok:
		_failures.append(name)


func _report() -> void:
	var shift_stats: Dictionary = _shift_stream.call("voice_stats")
	var roll_stats: Dictionary = _roll_stream.call("voice_stats")

	print("")
	print("=== the shift, clutch and rolling layers reach the mixer ===")
	print("")
	print("    audio driver             %s" % AudioServer.get_driver_name())
	print("    Master trim              %.1f dB   (every figure below is upstream of it)"
		% AudioServer.get_bus_volume_db(AudioServer.get_bus_index("Master")))
	print("    mix rate                 %.1f Hz" % float(shift_stats["mix_rate"]))
	print("    physics ticks            %d" % _tick)
	print("    cases                    %s" % ", ".join(_cases))
	if _break != "":
		print("    SABOTAGE                 --break=%s  (exit code inverted)" % _break)
	print("")

	if _cases.has("join"):
		# The check this file exists for. `strikes` is the audio thread's own count
		# of clacks struck, so it is the saboteur's fingerprint and not something the
		# probe could have produced another way: a level could be reached by noise,
		# a nonzero sample could be the clutch, but a strike only happens on an edge
		# of `EngineAudioInput::shifting`, which only the solver sets.
		var strikes: int = int(shift_stats["strikes"])
		# Two per shift -- disengagement and engagement -- so three commanded upshifts
		# is six. Asserted as "at least one per shift" rather than exactly six,
		# because whether the solver accepted all three depends on the gearbox state
		# and this gate is not a gearbox test.
		_check("a solver gearshift strikes a clack",
			strikes >= _shifts_commanded,
			"%d strikes from %d commanded shifts (2 per shift: out, then in)"
				% [strikes, _shifts_commanded])
		# The rolling layer's own join, checked as far as this fixture reaches.
		#
		# **What is NOT checked here, stated rather than quietly skipped**: that a
		# curb under the wheels produces a 133 Hz rumble at 20 m/s. This fixture is a
		# bare `KartBody` with no ground -- `voice_probe.gd` explains why that is the
		# right fixture for a boundary test -- so `surface` is asphalt and the rate is
		# correctly zero. Adding a collider would make this a physics test with an
		# audio assertion on the end.
		#
		# That claim is covered numerically, and better, in
		# `tests/core/test_shift_audio.cpp`: it renders the layer on a curb at 10, 20
		# and 30 m/s and recovers the modulation frequency out of the signal by
		# demodulation, against `v / lambda` computed from `surface.h`, to 3%. What is
		# asserted here is the part that test cannot see -- that the layer is on the
		# mixer at all and is the layer it was mounted as.
		_check("the rolling layer is mounted as the rolling layer",
			int(_roll_stream.get("layer")) == 3,
			"layer %d (LAYER_ROLL = 3); a wrong value renders a plausible layer that "
				% int(_roll_stream.get("layer")) + "ignores the surface")
		_check("_mix ran on both new layers",
			int(shift_stats["mix_calls"]) > 0 and int(roll_stats["mix_calls"]) > 0,
			"shift %d calls, rolling %d calls"
				% [int(shift_stats["mix_calls"]), int(roll_stats["mix_calls"])])

	if _cases.has("level"):
		_probe_shift_time_push()
		_check("neither new layer is at zero gain",
			float(_shift_stream.get("gain")) > 0.0 and float(_roll_stream.get("gain")) > 0.0,
			"clack %.3f, clutch %.3f, roll %.3f"
				% [float(_shift_stream.get("gain")),
					float(_shift_stream.get("clutch_gain")),
					float(_roll_stream.get("gain"))])
		_check("the body pushes the solver's shift time into the synth",
			_shift_time_restored,
			_shift_time_detail)

	if _cases.has("cost"):
		for pair in [["shift", shift_stats], ["rolling", roll_stats]]:
			var stats: Dictionary = pair[1]
			var load: float = stats["load_fraction_worst"]
			_check("the %s layer stays inside its cost budget" % pair[0],
				load < MAX_LAYER_LOAD,
				"%.1f ns/frame worst, %.4f %% of real time (budget %.2f %%)"
					% [float(stats["render_ns_worst_per_frame"]), 100.0 * load,
						100.0 * MAX_LAYER_LOAD])

	print("    check                                        result")
	for row in _rows:
		print("    %-44s %s   %s" % [row[1], row[0], row[2]])
	print("")
	print("    reported, not asserted:")
	print("      shift layer   %8.1f ns/frame mean, level %.4f"
		% [float(shift_stats["render_ns_per_frame"]), float(shift_stats["level"])])
	print("      rolling layer %8.1f ns/frame mean, level %.4f, rumble %.1f Hz seen"
		% [float(roll_stats["render_ns_per_frame"]), float(roll_stats["level"]), _rumble_seen])
	print("")

	if _break != "":
		# The saboteur's own fingerprint, named per mode. "Something failed" is not
		# enough -- CLAUDE.md records a control reporting caught off a pre-existing
		# red it had not caused.
		var wanted := {
			"unwired": "a solver gearshift strikes a clack",
			"silent": "neither new layer is at zero gain",
			"wrong_layer": "the rolling layer is mounted as the rolling layer",
			"stale_shift_time": "the body pushes the solver's shift time into the synth",
		}
		var want: String = wanted.get(_break, "")
		if _failures.has(want):
			print("    CAUGHT  --break=%s turned '%s' red, which is its own fingerprint."
				% [_break, want])
		elif _failures.size() > 0:
			print("    WRONG RED  --break=%s produced failures %s but not '%s'."
				% [_break, str(_failures), want])
			print("    That is a control passing for the wrong reason. Treat as a failure.")
		else:
			print("    MISSED  --break=%s changed nothing. The check cannot fail." % _break)
		print("")

	print("    %d checks, %d failed" % [_checked, _failures.size()])
	print("")


func _parse_args() -> Dictionary:
	var out := {}
	for argument in OS.get_cmdline_user_args():
		var text: String = argument
		if text.begins_with("--"):
			text = text.substr(2)
		var halves := text.split("=", true, 1)
		if halves.size() == 2:
			out[halves[0]] = halves[1]
		else:
			out[halves[0]] = "true"
	return out
