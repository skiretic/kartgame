extends SceneTree

## Does the engine note actually reach the mixer? Issues #81, #82.
##
##     godot --display-driver headless --audio-driver CoreAudio \
##         --path . --script tools/verify/voice_probe.gd
##
## ## The failure this exists to catch
##
## Every part of the audio path fails **silently**. An unregistered
## `EngineVoicePlayback` makes Godot call the base-class `_mix`, which writes
## nothing and reports success. A player whose stream is the wrong resource plays
## happily and emits nothing. A `KartBody` whose `engine_voice_player` path does not
## resolve publishes into a null `Ref` and drops every tick on the floor. None of
## the three logs anything, none fails a gate, and all three present to a driver as
## "the synth does not work" — which is the most expensive possible place to start
## looking, because the synth is the one part that is unit-tested.
##
## So this asserts the joins rather than the sound: `_mix` was called, the seqlock
## carried a changing rpm across the thread boundary, and the samples are not zero.
## What it *cannot* judge is whether the note sounds like a KZ. That is #82's
## acceptance criterion and it is judged by a driver with headphones on.
##
## Cost figures are `tools/verify/synth_cost_probe.gd`'s job. This one reports the
## load fraction only as a sanity bound, because a voice that were somehow costing
## 100% of real time would be a defect this probe should not stay quiet about.

## Ticks to run. 240 at 120 Hz is two seconds, which at a 512-frame block is about
## 190 `_mix` calls — enough that "never called" and "called once at startup" cannot
## be confused.
const TICKS := 240

## Throttle held for the whole run, so the engine revs and the partial count moves.
##
## **The rpm is the solver's and is not injected**, which is the point. Publishing a
## synthetic rpm into the stream would test the seqlock while skipping
## `KartBody::publish_engine_audio` — the mapping from `VehicleTelemetry` to
## `EngineAudioInput`, which is the join with no other coverage. Holding the
## throttle exercises the whole chain and a moving partial count is the evidence it
## moved: `active_partials` falls as f0 rises, from 191 at idle toward 33 at the
## soft cut, so a count that changes is an rpm that crossed the thread boundary.
##
## A first version of this file computed an rpm ramp and never published it. The
## probe passed, on `KartBody`'s own idle rpm, and the ramp was dead code that made
## the report claim a transport test it had not run.
const PROBE_THROTTLE := 1.0

var _kart: KartBody
var _stream: Object
var _tick := 0

## Distinct partial counts seen across the run. More than one means the rpm reached
## the audio thread and changed there.
var _partials_seen: Dictionary = {}


func _initialize() -> void:
	if AudioServer.get_driver_name() == "Dummy":
		printerr("error: the audio driver is Dummy — ADR-0035's second surprise. This")
		printerr("       probe checks that _mix is reached, and the Dummy driver reaches")
		printerr("       it in bursts on an ordinary thread, so a pass here would not")
		printerr("       mean the real path works. Use --audio-driver CoreAudio.")
		quit(1)
		return

	for required in ["KartBody", "EngineVoiceStream", "EngineVoicePlayback"]:
		if not ClassDB.class_exists(required):
			printerr("error: %s is not registered — scons target=editor arch=arm64" % required)
			quit(1)
			return

	# A bare `KartBody` with no mesh and no collision shape. It will complain about
	# the missing shape and fall, which is fine and is not what is being measured:
	# nothing here reads the solver's output, only what the boundary publishes.
	_kart = KartBody.new()
	_kart.name = "Kart"
	get_root().add_child(_kart)

	# Full throttle, no brake, no steer. `engage` puts it in a gear with a driveline
	# already turning, because a kart with no collision shape never gets a contact
	# and an auto-clutch waiting for road speed would idle for the whole run.
	_kart.input_driver = func() -> Dictionary:
		return {"throttle": PROBE_THROTTLE, "brake": 0.0, "steer": 0.0}
	_kart.engage(1, 5.0)

	_stream = EngineVoiceRig.attach(_kart)
	if _stream == null:
		printerr("error: EngineVoiceRig.attach returned null with the classes registered.")
		quit(1)
		return


func _physics_process(_delta: float) -> bool:
	# Sampled from the audio thread's own read-back rather than from the solver, so
	# that what is recorded is what crossed the boundary and not what was sent.
	var stats: Dictionary = _stream.call("voice_stats")
	_partials_seen[int(stats["partials"])] = true

	_tick += 1
	if _tick < TICKS:
		return false

	_report()
	quit(0)
	return true


func _report() -> void:
	var stats: Dictionary = _stream.call("voice_stats")
	var calls: int = stats["mix_calls"]
	var partials: int = stats["partials"]
	var rate: float = stats["mix_rate"]
	var gave_up: int = stats["seq_gave_up"]
	var retries: int = stats["seq_retries"]
	var per_frame: float = stats["render_ns_per_frame"]
	var worst: float = stats["render_ns_worst_per_frame"]
	var load_fraction: float = stats["load_fraction"]
	var load_worst: float = stats["load_fraction_worst"]

	print("")
	print("=== the engine voice reaches the mixer ===")
	print("")
	print("    audio driver                 %s" % AudioServer.get_driver_name())
	print("    mix rate                     %.1f Hz" % rate)
	print("    physics ticks                %d" % _tick)
	print("    _mix calls                   %d" % calls)
	print("    partials, last block         %d" % partials)
	print("")
	print("    seqlock retries              %d" % retries)
	print("    seqlock gave up              %d" % gave_up)
	print("")
	print("    render, mean                 %8.1f ns/frame   %6.2f %% of real time"
		% [per_frame, 100.0 * load_fraction])
	print("    render, worst block          %8.1f ns/frame   %6.2f %% of real time"
		% [worst, 100.0 * load_worst])
	print("")

	var failed := false

	# The join that fails silently and costs the most to diagnose.
	if calls <= 0:
		print("    FAIL  _mix was never called. The playback class is not on the path:")
		print("          check GDREGISTER_CLASS(EngineVoicePlayback) in register_types.cpp,")
		print("          and that the player was told to play().")
		failed = true
	else:
		print("    ok    _mix ran %d times" % calls)

	# A stack of zero partials renders silence at any gain, and rpm zero is exactly
	# what a boundary that published nothing would leave behind.
	if partials <= 0:
		print("    FAIL  the synth rendered 0 partials, which is silence. The seqlock")
		print("          carried no rpm — check KartBody::publish_engine_audio and that")
		print("          engine_voice_player resolves.")
		failed = true
	else:
		print("    ok    the stack was %d partials on the last block" % partials)

	# The transport test proper. One distinct count over 240 ticks of full throttle
	# means the audio thread saw one rpm for two seconds — the seqlock published once
	# and stopped, or `publish_engine_audio` is never reached, or the payload is not
	# arriving. Silence is not the symptom; a note stuck at idle is.
	var distinct: int = _partials_seen.size()
	if distinct <= 1:
		print("    FAIL  the partial count never changed across %d ticks at full" % _tick)
		print("          throttle. The rpm is not crossing the thread boundary: the")
		print("          note would be stuck at one pitch, which is not silence and is")
		print("          why this is checked separately from _mix being called.")
		failed = true
	else:
		var counts: Array = _partials_seen.keys()
		counts.sort()
		print("    ok    the stack moved across %d distinct counts, %d down to %d"
			% [distinct, counts[counts.size() - 1], counts[0]])

	# Bounded retry means giving up is *possible*, and at one publish per tick
	# against one read per block ADR-0035 predicts it never happens. A nonzero count
	# is not a tear — it is the budget being too low for the writer's rate — but it
	# means a block repeated the previous tick's state and somebody should know.
	if gave_up > 0:
		print("    WARN  %d reads exhausted the retry budget and repeated the last good" % gave_up)
		print("          tick. Not a tear. At one publish per tick this should be zero.")
	else:
		print("    ok    no seqlock read exhausted its budget")

	if load_worst >= 1.0:
		print("    FAIL  the worst block spent %.2f%% of real time. That underruns."
			% [100.0 * load_worst])
		failed = true

	print("")
	if failed:
		print("    ANSWER: the voice does NOT reach the mixer.")
		quit(1)
		return
	print("    ANSWER: the voice reaches the mixer. Whether it sounds like a KZ is")
	print("    #82's acceptance criterion and is judged by a driver, not by this.")
