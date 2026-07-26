extends SceneTree

## Issue #81 — measure what Godot 4.7.1's audio boundary actually is, before any
## DSP is written against it.
##
##     tools/verify/audio_probe.sh            both audio drivers, side by side
##     godot --display-driver headless --audio-driver CoreAudio --path . \
##         --script tools/verify/audio_probe.gd
##     godot --headless --path . --script tools/verify/audio_probe.gd
##
## **Run it under a real driver.** `--headless` selects the Dummy audio driver,
## and the Dummy driver is not a quiet CoreAudio: its mix thread runs on an
## ordinary core at ordinary speed and calls back in irregular bursts, while
## CoreAudio's runs at 2.5x the integer cost and 6x the floating-point cost of the
## main thread, on a metronomic deadline. Every cost figure taken headless is
## understated by about 6x. The wrapper runs both for exactly that reason.
##
## Arguments, all after a bare `--`:
##
##     --partials=N     harmonics the throwaway stack renders in `_mix` (24)
##     --busy-us=N      microseconds `_physics_process` spends holding its
##                      overlap window open (2000); 0 disables the window
##     --seconds=N      how long the measurement phase runs (6)
##     --buffer=S       AudioStreamGenerator buffer_length, seconds (0.10)
##     --no-generator   skip section 3 entirely
##     --table          render the stack from an interpolated sine table inside
##                      `_mix` instead of `std::sin`
##     --audible        actually make a sound. Off by default: the stack is
##                      rendered at zero gain, which costs exactly the same and
##                      is much easier to be in a room with.
##     --torture        publish continuously and read 2048 times per block, to
##                      show that an unsynchronized handoff *can* tear. A
##                      different question from the default run, so it is a
##                      separate one — see section 4.
##
## ## Why this file exists
##
## `ARCHITECTURE.md` §12 says "engine note synthesized live … `AudioStreamGenerator`
## fed from C++". Issue #81's title repeats that and then its acceptance criterion
## says "no locks or allocation **on the audio thread**". Those are two different
## architectures and `src/core/audio_state.h` says so in its own header:
##
##   (a) `AudioStreamGenerator` + `AudioStreamGeneratorPlayback.push_buffer`, a
##       ring filled from `_process` on the main thread. There is then no audio
##       thread in user code at all, the lock-free criterion is vacuous, and the
##       whole risk is underrun.
##   (b) a GDExtension `AudioStream`/`AudioStreamPlayback` pair overriding `_mix`.
##       If — and only if — Godot calls that on a genuine audio thread, the
##       criterion is load-bearing and the state handoff needs a real lock-free
##       transport.
##
## Which one Godot provides is an **engine behavior**, so this project measures it
## rather than reading it. That is not a preference: ADR-0033 asked seven questions
## about the contact boundary and three of the answers contradicted things this
## project had already written down as settled, one of them in CLAUDE.md's trap
## list. `integration_probe.gd` (ADR-0032) and `contact_probe.gd` (ADR-0033) are
## the two worked examples and this file is built the same way — **one question per
## case, an analytic prediction printed beside every measurement.**
##
## ## The six questions
##
##   1. **Which thread runs `_mix`?** Capture the thread id inside `_mix` and on
##      the main thread and print both. If they are equal, (b) is not off-main and
##      "on the audio thread" describes nothing.
##   2. **What block sizes, how regularly?** A synth interpolates a 120 Hz input
##      across whatever block Godot asks for, so min/median/max of both the frame
##      count and the wall-clock interval are the numbers the interpolator is
##      designed against.
##   3. **What does `AudioStreamGenerator` actually give you?** Ring capacity
##      against the analytic `buffer_length * mix_rate`, drain rate against the
##      analytic mix rate, queue depth as a latency, and `get_skips()` under a
##      deliberately stalled producer.
##   4. **Is `_mix` serialized with the physics tick?** An atomic flag pair, not an
##      inference from thread ids: `_physics_process` holds a window open and
##      samples whether a mix is in flight, and `_mix` samples whether a window is
##      open. Both directions, because either alone can be explained away.
##   5. **Cost.** A 24-partial stack at 48 kHz, priced against `ARCHITECTURE.md`
##      §15's 0.5 ms of a 16.6 ms frame, and again at the partial count
##      `kz_audio::STACK_CEILING_HZ` actually implies at idle, and again at the
##      twelve karts M7 wants.
##   6. **Does the GDExtension `_mix` path survive at all?** Registration,
##      instantiation, and the CLAUDE.md `dyld4::callInitializer` trap — a
##      namespace-scope `StringName` in a GDExtension crashes before any Godot
##      frame exists, so `src/audio/audio_probe.cpp` has none.
##
## Plus the analogue of the `Viewport.set_measure_render_time` hang: section 0
## times every audio call made **before the first frame**, because `shoot.sh` had
## to work around exactly that class of defect on the rendering side.
##
## ## What is deterministic here and what cannot be
##
## Almost nothing. This probe measures a relationship between two clocks, so
## unlike `contact_probe.gd` it reads wall-clock time everywhere and its numbers
## are distributions rather than digits. Every quantity that is a duration is
## labeled as one. Run it more than once before believing a tail.

# --- configuration ---------------------------------------------------------

## Harmonics the throwaway `_mix` stack renders. 24 because that is
## `kz_audio::LADDER_MEASURED_TO`, which is what the task asked to be priced.
## Section 5 also prices the count the frequency ceiling implies, which is much
## larger and is the number that decides anything.
var _partials := 24

## Microseconds `_physics_process` spins with its overlap window raised. 2000 out
## of a 8333 us tick is a 24 % duty cycle — high enough that an asynchronous mixer
## cannot avoid landing inside it over thousands of ticks, low enough that the
## physics loop still keeps up and `max_physics_steps_per_frame` never clamps.
var _busy_us := 2000

## Seconds of measurement. 6 s at 120 Hz is 720 physics ticks and, at a 512-frame
## block and 48 kHz, about 560 mix calls — enough for a median to mean something
## and for a rare re-entrant call to have a chance to appear.
var _seconds := 6.0

## `AudioStreamGenerator.buffer_length`, seconds. 0.10 is Godot's own default. The
## analytic ring capacity is `buffer_length * mix_rate`, and whether the engine
## agrees is question 3's first measurement.
var _buffer_length := 0.10

var _run_generator := true
var _use_table := false
var _torture := false
var _audible := false

## The fundamental the throwaway stack runs at, Hz. A KZ at 13,000 rpm through
## `kz_audio::rpm_to_f0_hz` — 13000/60 — because §6.3 puts peak power there and
## because the cost of a sine stack does not depend on the pitch, so there is no
## reason to pick a number that is not the real one.
const FUNDAMENTAL_HZ := 13000.0 / 60.0

## Output gain for the throwaway stack. **Zero by default, and the cost
## measurement does not change because of it.**
##
## The gain is one multiply applied to the summed stack once per frame, after every
## partial has been evaluated — so at zero the synth still does all 24 sines, all
## 24 phase increments and all 24 multiply-adds per frame, and section 5 measures
## exactly the same work. What it does not do is come out of the speakers.
##
## That is not fastidiousness. Under a real driver this probe runs a 216 Hz
## 24-partial sawtooth-ish stack for the whole measurement window, and it is
## genuinely unpleasant. `--audible` turns it on for anyone who wants to confirm
## the path really reaches the device.
const PROBE_GAIN := 0.0
const PROBE_GAIN_AUDIBLE := 0.03

## Fraction of the measurement phase during which the generator's producer is
## deliberately stopped, to induce underrun. Starts at 60 % and lasts 15 %.
const STALL_START := 0.60
const STALL_END := 0.75

## §15's audio budget, milliseconds per 16.6 ms frame at 60 fps. Every cost below
## is quoted against this.
const AUDIO_BUDGET_MS := 0.5
const FRAME_MS := 1000.0 / 60.0

## Karts at M7. The per-kart costs are multiplied by this and quoted again,
## because a synth that fits once and not twelve times has not been shown to fit.
const M7_KARTS := 12

# --- state -----------------------------------------------------------------

var _probe: Object
var _probe_player: AudioStreamPlayer
var _gen_player: AudioStreamPlayer
var _gen_stream: AudioStreamGenerator
var _gen_playback: AudioStreamGeneratorPlayback

var _lines: Array[String] = []
var _startup: Array[Array] = []

var _tick := 0
var _total_ticks := 0
var _started := false
var _start_us := 0

## Ring capacity as reported before anything was ever pushed, and the analytic
## prediction for it.
var _gen_capacity := 0

## `get_frames_available()` sampled once per idle frame, and the total frames the
## producer pushed. Together they give the drain rate.
var _gen_available: Array[int] = []
var _gen_queue_frames: Array[int] = []
var _gen_pushed := 0
var _gen_push_calls := 0
var _gen_skips_before_stall := 0
var _gen_skips_after_stall := 0
var _gen_stalled_frames := 0
var _gen_idle_frames := 0
var _gen_first_push_us := 0
var _gen_last_push_us := 0
var _gen_push_cost_us := 0

## Buffer lengths the ring-capacity sweep asks for, seconds. Spread over more
## than an order of magnitude and deliberately not landing on round frame counts,
## because the question is what Godot rounds *to*: at 44.1 kHz these are 882,
## 2205, 4410, 8820 and 22050 frames, none of which is a power of two.
const RING_SWEEP: Array[float] = [0.02, 0.05, 0.10, 0.20, 0.50]

var _sweep_players: Array[AudioStreamPlayer] = []
var _sweep_capacity: Array[int] = []

## A frame of silence, allocated once. Pushing a freshly built PackedVector2Array
## every frame would be measuring GDScript's allocator, which is not the question
## — and is itself the argument against architecture (a) living in GDScript.
var _push_block: PackedVector2Array


func _initialize() -> void:
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--partials="):
			_partials = int(argument.substr(11))
		elif argument.begins_with("--busy-us="):
			_busy_us = int(argument.substr(10))
		elif argument.begins_with("--seconds="):
			_seconds = float(argument.substr(10))
		elif argument.begins_with("--buffer="):
			_buffer_length = float(argument.substr(9))
		elif argument == "--no-generator":
			_run_generator = false
		elif argument == "--audible":
			_audible = true
		elif argument == "--torture":
			# Publish continuously from the physics window and read 2048 times per
			# mix block, to find out whether the unsynchronized copy *can* tear.
			# Separate from the default run because it is a different question.
			_torture = true
		elif argument == "--table":
			# Render the stack from the interpolated table inside `_mix` rather than
			# with `std::sin`. A separate run and not a second column, because the
			# audio thread is measured as it actually behaves and two synths in one
			# `_mix` would each perturb the other's cache.
			_use_table = true

	_total_ticks = int(_seconds * float(Engine.physics_ticks_per_second))

	# --- section 0: does anything block before the first frame? --------------
	#
	# `Viewport.set_measure_render_time(true)` hangs before the first frame on
	# Godot's Metal backend, which cost `shoot.sh` a workaround. Nothing here has
	# been shown to have the same problem, and "has not been shown to" is exactly
	# the state that produced four M3a defects, so every audio call this file makes
	# before the first frame is timed and printed. A call that blocks on the device
	# would show as milliseconds where the rest show as microseconds.
	_time_startup("AudioServer.get_driver_name", func() -> Variant: return AudioServer.get_driver_name())
	_time_startup("AudioServer.get_mix_rate", func() -> Variant: return AudioServer.get_mix_rate())
	_time_startup("AudioServer.get_speaker_mode", func() -> Variant: return AudioServer.get_speaker_mode())
	_time_startup("AudioServer.get_output_latency", func() -> Variant: return AudioServer.get_output_latency())
	_time_startup("AudioServer.get_time_to_next_mix", func() -> Variant: return AudioServer.get_time_to_next_mix())
	_time_startup("AudioServer.get_time_since_last_mix", func() -> Variant: return AudioServer.get_time_since_last_mix())
	_time_startup("AudioServer.get_bus_count", func() -> Variant: return AudioServer.get_bus_count())
	_time_startup("ClassDB.instantiate(AudioProbe)", func() -> Variant: return ClassDB.instantiate("AudioProbe"))
	_time_startup("ClassDB.instantiate(AudioProbeStream)", func() -> Variant: return ClassDB.instantiate("AudioProbeStream"))

	# Question 6, first half: does the extension even expose the three classes?
	# A missing one here means the registration is wrong and every number below
	# would be measuring Godot's built-in silence.
	for class_name_ in ["AudioProbe", "AudioProbeStream", "AudioProbePlayback"]:
		if not ClassDB.class_exists(class_name_):
			printerr("error: %s is not registered — rebuild the extension" % class_name_)
			quit(1)
			return

	var mix_rate := AudioServer.get_mix_rate()

	_probe = ClassDB.instantiate("AudioProbe")
	_probe.set_partials(_partials)
	_probe.set_mix_rate(mix_rate)
	_probe.set_fundamental_hz(FUNDAMENTAL_HZ)
	_probe.set_gain(PROBE_GAIN_AUDIBLE if _audible else PROBE_GAIN)
	_probe.set_use_table(_use_table)
	_probe.set_torture(_torture)
	_probe.set_physics_busy_us(_busy_us)
	_probe.arm()
	get_root().add_child(_probe)

	# --- architecture (b): the custom stream ---------------------------------
	var probe_stream: AudioStream = ClassDB.instantiate("AudioProbeStream")
	_probe_player = AudioStreamPlayer.new()
	_probe_player.stream = probe_stream
	# Belt and braces with the zero gain above. A player at -80 dB is still mixed —
	# Godot does not skip `_mix` for a quiet player, and section 2's call counts
	# confirm it — so the measurement is untouched and the room stays quiet.
	_probe_player.volume_db = 0.0 if _audible else -80.0
	get_root().add_child(_probe_player)

	# --- architecture (a): the generator -------------------------------------
	if _run_generator:
		_gen_stream = AudioStreamGenerator.new()
		_gen_stream.mix_rate_mode = AudioStreamGenerator.MIX_RATE_OUTPUT
		_gen_stream.buffer_length = _buffer_length
		_gen_player = AudioStreamPlayer.new()
		_gen_player.stream = _gen_stream
		# Silence: this half of the probe is about the ring, not about the sound.
		_gen_player.volume_db = -80.0
		get_root().add_child(_gen_player)

		# Sized at one 120 Hz tick of audio, which is what a real producer running
		# from `_physics_process` would have to push. At 48 kHz that is 400 frames.
		var tick_frames := int(round(mix_rate / float(Engine.physics_ticks_per_second)))
		_push_block.resize(tick_frames)
		for i in range(tick_frames):
			_push_block[i] = Vector2.ZERO

		# The ring-capacity sweep. One silent player per buffer_length, so the
		# relationship between what is asked for and what is allocated can be seen
		# rather than inferred from a single point. Played and read on the first
		# physics tick and then stopped.
		for length in RING_SWEEP:
			var stream := AudioStreamGenerator.new()
			stream.mix_rate_mode = AudioStreamGenerator.MIX_RATE_OUTPUT
			stream.buffer_length = length
			var player := AudioStreamPlayer.new()
			player.stream = stream
			player.volume_db = -80.0
			get_root().add_child(player)
			_sweep_players.append(player)


func _time_startup(label: String, fn: Callable) -> void:
	var before := Time.get_ticks_usec()
	var value: Variant = fn.call()
	var after := Time.get_ticks_usec()
	if value is Object and value != null and not (value is RefCounted):
		(value as Object).free()
		value = "<instantiated>"
	_startup.append([label, after - before, value])


func _physics_process(delta: float) -> bool:
	if not _started:
		_started = true
		_start_us = Time.get_ticks_usec()
		_probe_player.play()
		if _gen_player != null:
			_gen_player.play()
			_gen_playback = _gen_player.get_stream_playback() as AudioStreamGeneratorPlayback
			# The ring's capacity, read before a single frame has been pushed or
			# consumed. This is the measurement; `buffer_length * mix_rate` is the
			# prediction, and section 3 prints them side by side.
			if _gen_playback != null:
				_gen_capacity = _gen_playback.get_frames_available()
		for player in _sweep_players:
			player.play()
			var playback := player.get_stream_playback() as AudioStreamGeneratorPlayback
			_sweep_capacity.append(playback.get_frames_available() if playback != null else -1)
			# Stopped immediately: five extra generators draining for the whole run
			# would compete with the one being measured for mixer time.
			player.stop()
		return false

	_tick += 1
	if _tick < _total_ticks:
		return false

	_report()
	quit(0)
	return true


func _process(_delta: float) -> bool:
	# The producer for architecture (a). It runs in the idle frame on purpose:
	# that is where §12's "fed from C++" would put it, and the whole point of (a)
	# is that the filling happens somewhere that is not the audio thread.
	if _gen_playback == null:
		return false

	_gen_idle_frames += 1
	var available := _gen_playback.get_frames_available()
	_gen_available.append(available)

	var progress := float(_tick) / maxf(float(_total_ticks), 1.0)
	if progress >= STALL_START and progress < STALL_END:
		# The deliberate producer stall. Nothing is pushed, the ring drains, and
		# `get_skips()` counts what the mixer could not find.
		if _gen_stalled_frames == 0:
			_gen_skips_before_stall = _gen_playback.get_skips()
		_gen_stalled_frames += 1
		return false

	if _gen_stalled_frames > 0 and _gen_skips_after_stall == 0:
		_gen_skips_after_stall = _gen_playback.get_skips()

	# Queue depth *before* the push is the latency the newly pushed sample will
	# wait through inside the ring, in frames. That plus the device's own output
	# latency is the end-to-end figure; the two are printed separately because
	# only the first one is a consequence of this architecture.
	_gen_queue_frames.append(_gen_capacity - available)

	while _gen_playback.can_push_buffer(_push_block.size()):
		var before := Time.get_ticks_usec()
		if not _gen_playback.push_buffer(_push_block):
			break
		_gen_push_cost_us += Time.get_ticks_usec() - before
		_gen_pushed += _push_block.size()
		_gen_push_calls += 1
		if _gen_first_push_us == 0:
			_gen_first_push_us = before
		_gen_last_push_us = Time.get_ticks_usec()

	return false


# --- report ----------------------------------------------------------------


func _report() -> void:
	var elapsed_us := Time.get_ticks_usec() - _start_us
	var report: Dictionary = _probe.report()

	_section_environment(elapsed_us)
	_section_startup()
	_section_threads(report)
	_section_blocks(report, elapsed_us)
	_section_generator(elapsed_us)
	_section_overlap(report, elapsed_us)
	_section_cost()
	_section_transport(report)

	print("\n".join(_lines))


func _section_environment(elapsed_us: int) -> void:
	_lines.append("")
	_lines.append("=== 0. environment ====================================================")
	_lines.append("")
	_lines.append("    godot                        %s" % Engine.get_version_info()["string"])
	_lines.append("    display driver               %s" % DisplayServer.get_name())
	_lines.append("    audio driver                 %s" % AudioServer.get_driver_name())
	_lines.append("    mix rate                     %.1f Hz" % AudioServer.get_mix_rate())
	_lines.append("    speaker mode                 %d" % AudioServer.get_speaker_mode())
	_lines.append("    output latency               %.6f s" % AudioServer.get_output_latency())
	_lines.append("    physics ticks per second     %d" % Engine.physics_ticks_per_second)
	_lines.append("    processor count              %d" % OS.get_processor_count())
	_lines.append("    measurement window           %.4f s" % (float(elapsed_us) / 1e6))
	_lines.append("    partials in _mix             %d" % _partials)
	_lines.append("    _mix renderer                %s" % ("interpolated table" if _use_table else "std::sin"))
	_lines.append("    transport torture            %s" % ("on" if _torture else "off"))
	_lines.append("    physics busy window          %d us of %d us tick" % [
		_busy_us, int(1e6 / float(Engine.physics_ticks_per_second)),
	])


func _section_startup() -> void:
	_lines.append("")
	_lines.append("=== 0b. does anything block before the first frame? ===================")
	_lines.append("")
	_lines.append("    The audio analogue of the `Viewport.set_measure_render_time` hang.")
	_lines.append("    Every one of these ran from `_initialize`, before Godot's first")
	_lines.append("    frame. A call that waits on the device would show milliseconds.")
	_lines.append("")
	_lines.append("    %-40s %10s   %s" % ["call", "us", "returned"])
	for row in _startup:
		_lines.append("    %-40s %10d   %s" % [row[0], row[1], str(row[2])])


func _section_threads(report: Dictionary) -> void:
	var hashes: PackedInt64Array = report["mix_thread_hashes"]
	var main_hash: int = report["main_thread_hash"]
	var off_main := false
	for h in hashes:
		if h != main_hash:
			off_main = true

	_lines.append("")
	_lines.append("=== 1. which thread runs AudioStreamPlayback::_mix? ===================")
	_lines.append("")
	_lines.append("    Two candidate answers, both plausible before the measurement:")
	_lines.append("      A: the same thread as `_ready` — Godot mixes inside the main loop")
	_lines.append("         and `_mix` is not on an audio thread at all.")
	_lines.append("      B: a different thread — the audio server owns a mixer thread and")
	_lines.append("         issue #81's 'no locks on the audio thread' is load-bearing.")
	_lines.append("")
	_lines.append("    main thread, std::hash        %d" % main_hash)
	_lines.append("    main thread, Godot caller id  %d" % report["main_thread_godot_id"])
	_lines.append("    _mix thread, Godot caller id  %d" % report["mix_godot_id"])
	_lines.append("    distinct _mix threads seen    %d" % hashes.size())
	for i in range(hashes.size()):
		_lines.append("      [%d] std::hash              %d%s" % [
			i, hashes[i], "   == main" if hashes[i] == main_hash else "   != main",
		])
	_lines.append("")
	if hashes.size() == 0:
		_lines.append("    ANSWER  _mix was never called. Nothing below section 3 is trustworthy.")
	elif off_main:
		_lines.append("    ANSWER  B. _mix runs off the main thread.")
	else:
		_lines.append("    ANSWER  A. _mix runs on the main thread.")


func _section_blocks(report: Dictionary, elapsed_us: int) -> void:
	var frames: PackedInt32Array = _probe.call_frames()
	var intervals: PackedInt64Array = _probe.call_intervals_ns()

	_lines.append("")
	_lines.append("=== 2. block size and call regularity =================================")
	_lines.append("")
	_lines.append("    mix calls                    %d" % report["mix_calls"])
	_lines.append("    frames mixed                 %d" % report["mix_frames_total"])
	_lines.append("    calls recorded individually  %d" % frames.size())

	if frames.size() == 0:
		_lines.append("")
		_lines.append("    _mix was never called in this configuration.")
		return

	var rate: float = AudioServer.get_mix_rate()
	var seconds := float(elapsed_us) / 1e6
	# The analytic prediction: a mixer that keeps up produces exactly `rate`
	# frames per wall-clock second, so total frames / seconds must land on the mix
	# rate. It landing anywhere else means the audio clock is not running at the
	# rate the server reports — which under a dummy driver is exactly what would
	# happen and is why this line is here.
	var measured_rate := float(report["mix_frames_total"]) / maxf(seconds, 1e-9)
	_lines.append("")
	_lines.append("    %-28s %14s %14s" % ["", "measured", "analytic"])
	_lines.append("    %-28s %14.1f %14.1f" % ["frames per wall second", measured_rate, rate])
	_lines.append("    %-28s %14.4f %14.4f" % [
		"audio seconds per wall s", measured_rate / rate, 1.0,
	])

	var f_stats := _int_stats_i32(frames)
	_lines.append("")
	_lines.append("    p_frames        min %d   median %d   max %d   mean %.1f" % [
		f_stats["min"], f_stats["median"], f_stats["max"], f_stats["mean"],
	])
	_lines.append("    distinct block sizes         %d" % _distinct_i32(frames).size())
	_lines.append("    block sizes seen             %s" % str(_distinct_i32(frames).slice(0, 8)))
	_lines.append("")
	_lines.append("    A block of N frames is %.3f ms of audio at %.0f Hz, and the solver" % [
		1000.0 * float(f_stats["median"]) / rate, rate,
	])
	_lines.append("    publishes every %.3f ms. The synth must interpolate its input across" % (
		1000.0 / float(Engine.physics_ticks_per_second)
	))
	_lines.append("    %.2f solver ticks per block at the median." % (
		float(f_stats["median"]) / rate * float(Engine.physics_ticks_per_second)
	))

	# Intervals. Element 0 is the first call and has no predecessor, so it is
	# dropped rather than counted as a zero-length interval.
	var trimmed: PackedInt64Array = PackedInt64Array()
	for i in range(1, intervals.size()):
		trimmed.append(intervals[i])
	if trimmed.size() > 0:
		var i_stats := _int_stats_i64(trimmed)
		var predicted_us := 1e6 * float(f_stats["median"]) / rate
		_lines.append("")
		_lines.append("    %-28s %14s %14s" % ["interval between calls", "measured us", "analytic us"])
		_lines.append("    %-28s %14.1f %14.1f" % [
			"median", float(i_stats["median"]) / 1000.0, predicted_us,
		])
		_lines.append("    %-28s %14.1f %14s" % ["min", float(i_stats["min"]) / 1000.0, "—"])
		_lines.append("    %-28s %14.1f %14s" % ["max", float(i_stats["max"]) / 1000.0, "—"])
		_lines.append("    %-28s %14.1f %14s" % ["mean", float(i_stats["mean"]) / 1000.0, "—"])
		_lines.append("")
		_lines.append("    The analytic interval is the median block divided by the mix rate:")
		_lines.append("    a mixer that keeps up calls back exactly as fast as it drains. A max")
		_lines.append("    far above it is a scheduling stall and is what an underrun looks")
		_lines.append("    like from inside `_mix`.")


func _section_generator(elapsed_us: int) -> void:
	_lines.append("")
	_lines.append("=== 3. what AudioStreamGenerator actually gives you ===================")
	_lines.append("")
	if _gen_playback == null:
		_lines.append("    Not run.")
		return

	var rate: float = AudioServer.get_mix_rate()
	var seconds := float(elapsed_us) / 1e6
	var analytic_capacity := int(round(_buffer_length * rate))

	_lines.append("    buffer_length requested      %.4f s" % _buffer_length)
	_lines.append("    generator mix_rate           %.1f Hz  (mode %d)" % [
		_gen_stream.mix_rate, _gen_stream.mix_rate_mode,
	])
	_lines.append("")
	_lines.append("    %-28s %14s %14s" % ["", "measured", "analytic"])
	_lines.append("    %-28s %14d %14d" % [
		"ring capacity, frames", _gen_capacity, analytic_capacity,
	])
	_lines.append("    %-28s %14.4f %14.4f" % [
		"ring capacity, seconds", float(_gen_capacity) / rate, _buffer_length,
	])
	_lines.append("    %-28s %14.1f %14.1f" % [
		"drained frames per wall s", float(_gen_pushed) / maxf(seconds, 1e-9), rate,
	])
	if _sweep_capacity.size() == RING_SWEEP.size():
		_lines.append("")
		_lines.append("    The ring is not the size you asked for. Sweeping buffer_length:")
		_lines.append("")
		_lines.append("    %-12s %12s %12s %12s %10s" % [
			"asked, s", "asked, fr", "got, fr", "pow2-1", "got, s",
		])
		for i in range(RING_SWEEP.size()):
			var asked_frames := int(round(RING_SWEEP[i] * rate))
			# The hypothesis the sweep tests: Godot rounds the ring up to a power of
			# two and keeps one frame back to tell full from empty. Predicted
			# capacity is therefore next_pow2(asked) - 1.
			var pow2 := 1
			while pow2 < asked_frames:
				pow2 *= 2
			_lines.append("    %-12.3f %12d %12d %12d %10.4f" % [
				RING_SWEEP[i], asked_frames, _sweep_capacity[i], pow2 - 1,
				float(_sweep_capacity[i]) / rate,
			])
		_lines.append("")
		_lines.append("    If the `got` and `pow2-1` columns agree, `buffer_length` is a request")
		_lines.append("    and not a setting, and the latency it buys is up to 2x what it names.")

	_lines.append("")
	_lines.append("    The drain rate is inferred from what the producer had to push to keep")
	_lines.append("    the ring full: over a long run, pushed == consumed. Its analytic value")
	_lines.append("    is the mix rate, and a shortfall means the ring never filled.")
	_lines.append("")
	_lines.append("    frames pushed                %d" % _gen_pushed)
	_lines.append("    push_buffer calls            %d" % _gen_push_calls)
	_lines.append("    idle frames                  %d" % _gen_idle_frames)
	_lines.append("    idle frames per wall s       %.1f" % (float(_gen_idle_frames) / maxf(seconds, 1e-9)))
	if _gen_push_calls > 0:
		_lines.append("    mean push_buffer cost        %.1f us for %d frames" % [
			float(_gen_push_cost_us) / float(_gen_push_calls), _push_block.size(),
		])

	if _gen_available.size() > 0:
		var a_stats := _int_stats_array(_gen_available)
		_lines.append("")
		_lines.append("    get_frames_available()       min %d   median %d   max %d" % [
			a_stats["min"], a_stats["median"], a_stats["max"],
		])
	if _gen_queue_frames.size() > 0:
		var q_stats := _int_stats_array(_gen_queue_frames)
		_lines.append("")
		_lines.append("    Queue depth at the moment of a push — how much audio a freshly")
		_lines.append("    pushed sample waits behind inside the ring. This is the latency")
		_lines.append("    architecture (a) adds; the device's own output latency is on top.")
		_lines.append("")
		_lines.append("    %-28s %14s %14s" % ["", "measured", "analytic"])
		_lines.append("    %-28s %14.2f %14.2f" % [
			"ring latency, ms, median", 1000.0 * float(q_stats["median"]) / rate,
			1000.0 * float(_gen_capacity) / rate,
		])
		_lines.append("    %-28s %14.2f %14s" % [
			"ring latency, ms, max", 1000.0 * float(q_stats["max"]) / rate, "—",
		])
		_lines.append("    %-28s %14.2f %14s" % [
			"output latency, ms", 1000.0 * AudioServer.get_output_latency(), "—",
		])
		_lines.append("")
		_lines.append("    The analytic figure is the whole ring: a producer that keeps it")
		_lines.append("    topped up pays the full buffer_length in latency, which is the")
		_lines.append("    cost of architecture (a) and the reason buffer_length is a")
		_lines.append("    latency knob and not a safety knob.")

	_lines.append("")
	_lines.append("    Underrun, with the producer deliberately stopped:")
	_lines.append("      idle frames stalled        %d" % _gen_stalled_frames)
	_lines.append("      get_skips() before stall    %d" % _gen_skips_before_stall)
	_lines.append("      get_skips() after stall     %d" % _gen_skips_after_stall)
	_lines.append("      get_skips() at end          %d" % _gen_playback.get_skips())
	_lines.append("      frames_available at end     %d" % _gen_playback.get_frames_available())


func _section_overlap(report: Dictionary, elapsed_us: int) -> void:
	var frames: PackedInt32Array = _probe.call_frames()
	var synth: PackedInt64Array = _probe.call_synth_ns()
	var intervals: PackedInt64Array = _probe.call_intervals_ns()

	_lines.append("")
	_lines.append("=== 4. is _mix serialized with the physics tick? ======================")
	_lines.append("")
	_lines.append("    Measured with an atomic flag pair rather than inferred from thread")
	_lines.append("    ids, because two threads can still be serialized by a lock inside the")
	_lines.append("    server and a thread id cannot see that. `_physics_process` raises a")
	_lines.append("    window for %d us and polls whether a mix is in flight; `_mix` polls" % _busy_us)
	_lines.append("    whether a window is open. Both directions must agree.")
	_lines.append("")
	_lines.append("    physics ticks                %d" % report["physics_ticks"])
	_lines.append("    physics ticks overlapping    %d" % report["physics_ticks_overlapped"])
	_lines.append("    physics samples taken        %d" % report["physics_samples"])
	_lines.append("    samples with a mix in flight %d" % report["physics_saw_mix"])
	_lines.append("    mix calls seeing a window    %d of %d" % [
		report["mix_saw_physics"], report["mix_calls"],
	])
	_lines.append("    re-entrant _mix calls        %d" % report["mix_reentrant"])

	# Analytic predictions. If the mixer runs asynchronously, the fraction of
	# physics samples that catch a mix is the mix duty cycle — the synth's own
	# wall-clock divided by the interval between calls — and the fraction of
	# physics ticks that catch one at all is (window + mix duration) / interval,
	# capped at 1. If the mixer is serialized with the frame loop, both are
	# exactly zero and no sample size makes them otherwise.
	var mean_synth_ns := _mean_i64(synth)
	var mean_interval_ns := _mean_i64(intervals)
	if mean_interval_ns > 0.0:
		var duty := mean_synth_ns / mean_interval_ns
		var tick_ns := 1e9 / float(Engine.physics_ticks_per_second)
		var window_ns := float(_busy_us) * 1000.0
		var per_tick := minf((window_ns + mean_synth_ns) / mean_interval_ns, 1.0)
		var mix_window_share := minf(window_ns / tick_ns, 1.0)
		_lines.append("")
		_lines.append("    %-32s %12s %12s" % ["", "measured", "analytic"])
		_lines.append("    %-32s %12.5f %12.5f" % [
			"samples finding a mix",
			float(report["physics_saw_mix"]) / maxf(float(report["physics_samples"]), 1.0),
			duty,
		])
		_lines.append("    %-32s %12.5f %12.5f" % [
			"ticks overlapping at all",
			float(report["physics_ticks_overlapped"]) / maxf(float(report["physics_ticks"]), 1.0),
			per_tick,
		])
		_lines.append("    %-32s %12.5f %12.5f" % [
			"mix calls seeing a window",
			float(report["mix_saw_physics"]) / maxf(float(report["mix_calls"]), 1.0),
			mix_window_share,
		])
		_lines.append("")
		_lines.append("    The analytic column assumes the two run concurrently. The competing")
		_lines.append("    hypothesis — that Godot serializes them — predicts exactly 0.00000")
		_lines.append("    in all three rows, and no sample size changes that.")

	_lines.append("")
	_lines.append("    The transport experiment, running for the whole window above:")
	_lines.append("      seqlock reads from _mix    %d" % report["seq_reads"])
	_lines.append("      seqlock retries            %d" % report["seq_retries"])
	_lines.append("      seqlock torn snapshots     %d" % report["seq_torn"])
	_lines.append("      seqlock reads that gave up %d  (retry budget exhausted)" % report["seq_gave_up"])
	_lines.append("      unsynchronized reads       %d" % report["naked_reads"])
	_lines.append("      unsynchronized torn        %d" % report["naked_torn"])
	_lines.append("")
	_lines.append("    Both readers copy the same `EngineAudioInput` out of the same publish,")
	_lines.append("    one through a seqlock and one through nothing. Every field is derived")
	_lines.append("    from a single counter, so a snapshot that disagrees with itself is a")
	_lines.append("    tear and not an interpretation. `seqlock torn` must be 0.")
	_lines.append("")

	# A zero in `unsynchronized torn` is the most misleading number this file can
	# print, so it is not left to speak for itself. The analytic collision
	# probability is the fraction of wall-clock time a publish is in flight: the
	# publish rate times the width of the write window. At 120 Hz and a 3.5 ns
	# publish that is 4.2e-7, so a run of a few hundred reads is expected to find
	# exactly zero and would find exactly zero whether the transport were safe or
	# not. This is the shape of issue #107 — right numbers, wrong conclusion — and
	# the way out is to print the expectation next to the count.
	var facts: Dictionary = _probe.transport_facts(200000)
	var publish_ns: float = facts["seq_publish_ns"]
	# The publish rate is 120 Hz in the honest configuration. Under `--torture` the
	# window publishes continuously, so this model does not apply and the report
	# says so rather than printing a prediction it knows is wrong.
	var tear_probability := 120.0 * publish_ns / 1e9
	var reads: float = float(report["naked_reads"])
	var elapsed_s := float(elapsed_us) / 1e6
	var reads_per_second := reads / maxf(elapsed_s, 1e-9)

	_lines.append("    %-36s %12s %14s" % ["unsynchronized tears", "measured", "predicted"])
	if _torture:
		_lines.append("    %-36s %12d %14s" % [
			"over %d reads" % int(reads), report["naked_torn"], "n/a, torture",
		])
	else:
		_lines.append("    %-36s %12d %14.5f" % [
			"over %d reads" % int(reads), report["naked_torn"], reads * tear_probability,
		])
		# `%e` does not exist in GDScript's format — it leaves the literal in the
		# string rather than erroring, which is CLAUDE.md's trap and cost one
		# printed "%.2e" here. Stated as a reciprocal instead, which is the more
		# readable form anyway.
		_lines.append("    %-36s %12s %14d" % [
			"one tear per N reads", "—", int(1.0 / maxf(tear_probability, 1e-30)),
		])
		_lines.append("    %-36s %12s %14.3f" % [
			"tears per hour at %d reads/s" % int(reads_per_second), "—",
			tear_probability * reads_per_second * 3600.0,
		])
	_lines.append("")
	if _torture:
		_lines.append("    This is the torture configuration: the physics window publishes")
		_lines.append("    continuously and `_mix` reads %d times per block. It answers" % report["reads_per_mix"])
		_lines.append("    'can the unsynchronized copy tear at all', which a count of zero at")
		_lines.append("    the honest rate cannot. `seqlock torn` must still be exactly 0 —")
		_lines.append("    reads that exhausted the retry budget are counted separately and are")
		_lines.append("    a property of this artificial writer, not of the transport.")
	else:
		_lines.append("    A measured 0 here is not evidence of safety, and printing it without")
		_lines.append("    the prediction beside it is exactly the shape of issue #107. The")
		_lines.append("    publish window is %.1f ns wide at 120 Hz, so a reader lands inside" % publish_ns)
		_lines.append("    it about once in %d reads and this run took nowhere near that many." % int(1.0 / maxf(tear_probability, 1e-30)))
		_lines.append("    Run with `--torture` to raise the exposure by three orders of")
		_lines.append("    magnitude and watch the unsynchronized count come off zero while the")
		_lines.append("    seqlock stays on it. That is the comparison that decides anything;")
		_lines.append("    two zeros side by side decide nothing.")

	if frames.size() > 0:
		_lines.append("")
		_lines.append("    (mix duty cycle is measured over %d recorded calls in %.3f s)" % [
			frames.size(), float(elapsed_us) / 1e6,
		])


func _section_cost() -> void:
	_lines.append("")
	_lines.append("=== 5. cost, against the §15 budget ===================================")
	_lines.append("")
	_lines.append("    §15 gives audio %.1f ms of a %.2f ms frame at 60 fps, which is" % [
		AUDIO_BUDGET_MS, FRAME_MS,
	])
	_lines.append("    %.1f ms of wall-clock per second of audio — a %.2f %% duty cycle." % [
		AUDIO_BUDGET_MS * 60.0, 100.0 * AUDIO_BUDGET_MS / FRAME_MS,
	])
	_lines.append("")

	var rate: float = AudioServer.get_mix_rate()
	var budget_ms_per_audio_second := AUDIO_BUDGET_MS * 60.0

	# The partial counts that matter, and why each one is here.
	#
	#  24  — `kz_audio::LADDER_MEASURED_TO`, the number the ladder was measured to.
	#  37  — 8000 Hz / (13000 rpm / 60) = the ceiling at peak power.
	# 240  — 8000 Hz / (2000 rpm / 60) = the ceiling at idle, which is the worst
	#        case and is 10x the figure anyone quoting "24 partials" has in mind.
	var idle_partials := int(floor(8000.0 / (2000.0 / 60.0)))
	var peak_partials := int(floor(8000.0 / (13000.0 / 60.0)))
	# Each row carries the fundamental that makes its partial count physical. A
	# 240-partial stack at 13,000 rpm does not exist — its 111th harmonic is past
	# Nyquist — so pricing one would be pricing a stack nothing will ever render.
	# The cost per partial does not depend on f0; the row would just be a lie.
	var counts := [
		[1, 13000.0 / 60.0],
		[24, 13000.0 / 60.0],
		[peak_partials, 13000.0 / 60.0],
		[64, 5000.0 / 60.0],
		[idle_partials, 2000.0 / 60.0],
	]

	_lines.append("    %-10s %8s %12s %12s %12s %12s %12s" % [
		"partials", "f0 Hz", "sin ns/fr", "tbl ns/fr", "ms/audio s", "% budget", "% at 12",
	])
	for row in counts:
		var count: int = row[0]
		var f0: float = row[1]
		var bench: Dictionary = _probe.benchmark_synth(count, 4096, 64, f0)
		if bench.is_empty():
			continue
		var sin_ns: float = bench["sin_ns_per_frame"]
		var table_ns: float = bench["table_ns_per_frame"]
		var ms_per_audio_second := sin_ns * rate / 1e6
		_lines.append("    %-10d %8.1f %12.2f %12.2f %12.3f %12.1f %12.1f" % [
			count, f0, sin_ns, table_ns, ms_per_audio_second,
			100.0 * ms_per_audio_second / budget_ms_per_audio_second,
			100.0 * ms_per_audio_second * float(M7_KARTS) / budget_ms_per_audio_second,
		])

	_lines.append("")
	_lines.append("    Partial counts, and why each one is in the table:")
	_lines.append("      1    the floor — one `sin` per frame, the cost of the loop itself")
	_lines.append("      24   `kz_audio::LADDER_MEASURED_TO`, the number the ladder reaches")
	_lines.append("      %-3d  STACK_CEILING_HZ / f0 at 13,000 rpm — the ceiling at peak power" % peak_partials)
	_lines.append("      64   a middling rpm, for the shape of the curve")
	_lines.append("      %-3d  STACK_CEILING_HZ / f0 at a 2,000 rpm idle — the worst case" % idle_partials)
	_lines.append("")
	_lines.append("    The last row is the one that decides anything. `kz_audio_reference.h`")
	_lines.append("    says the stack fills to a **frequency** ceiling and not to a fixed")
	_lines.append("    harmonic index, precisely so the note does not change character across")
	_lines.append("    the rev range — and a frequency ceiling costs the most at idle, where")
	_lines.append("    the fundamental is lowest. A budget taken at 24 partials is a budget")
	_lines.append("    taken at the cheapest point in the range.")
	_lines.append("")
	_lines.append("    Measured on the main thread, so it excludes whatever the audio thread")
	_lines.append("    itself costs. Section 2's per-call synth time is the same work timed")
	_lines.append("    inside `_mix`, and the two are printed together below.")

	var synth: PackedInt64Array = _probe.call_synth_ns()
	var frames: PackedInt32Array = _probe.call_frames()
	if synth.size() > 0 and frames.size() == synth.size():
		# Per-call, not aggregated. The mean of a mean hides the thing that matters
		# here: whether the audio thread is *uniformly* slower than the main thread
		# or merely occasionally preempted. A median close to the mean says the
		# former, which is a property of the thread and not of the machine's mood.
		var per_frame: Array[int] = []
		var total_ns := 0.0
		var total_frames := 0.0
		for i in range(synth.size()):
			total_ns += float(synth[i])
			total_frames += float(frames[i])
			if frames[i] > 0:
				per_frame.append(int(round(float(synth[i]) / float(frames[i]))))
		var s_stats := _int_stats_array(per_frame)
		var in_mix_ns_per_frame := total_ns / maxf(total_frames, 1.0)
		var median_ns_per_frame := float(s_stats["median"])
		var in_mix_ms_per_second := median_ns_per_frame * rate / 1e6

		var bench24: Dictionary = _probe.benchmark_synth(_partials, 4096, 64, FUNDAMENTAL_HZ)
		var offline_key := "table_ns_per_frame" if _use_table else "sin_ns_per_frame"
		var offline_ns: float = bench24[offline_key] if not bench24.is_empty() else 0.0

		_lines.append("")
		_lines.append("    %-32s %12s %12s" % ["", "in _mix", "offline"])
		_lines.append("    %-32s %12.2f %12.2f" % [
			"ns per frame, median, %d partials" % _partials, median_ns_per_frame, offline_ns,
		])
		_lines.append("    %-32s %12.2f %12s" % ["ns per frame, mean", in_mix_ns_per_frame, "—"])
		_lines.append("    %-32s %12.2f %12s" % ["ns per frame, min", float(s_stats["min"]), "—"])
		_lines.append("    %-32s %12.2f %12s" % ["ns per frame, max", float(s_stats["max"]), "—"])
		_lines.append("    %-32s %12.3f %12s" % [
			"ms per second of audio, median", in_mix_ms_per_second, "—",
		])
		_lines.append("    %-32s %12.1f %12s" % [
			"% of the §15 budget", 100.0 * in_mix_ms_per_second / budget_ms_per_audio_second, "—",
		])
		if offline_ns > 0.0:
			_lines.append("    %-32s %12.2f %12s" % [
				"audio thread / main thread", median_ns_per_frame / offline_ns, "—",
			])
			_lines.append("")
			_lines.append("    That last ratio is the whole reason this row is here. Identical")
			_lines.append("    code, identical partial count, one run on the main thread and one")
			_lines.append("    inside `_mix`. A ratio near 1 would mean an offline benchmark can")
			_lines.append("    price the synth. Anything else means it cannot, and every cost in")
			_lines.append("    the table above has to be multiplied by it before being compared")
			_lines.append("    to §15.")

		# The integer calibration. Same dependent 64-bit chain, same iteration
		# count, run in the same `_mix` call and again on the main thread. If it is
		# slowed by the same factor as the synth, the audio thread is running on a
		# slower core and every kind of DSP pays it. If it is not, only floating
		# point is affected and the conclusion would be a different one. This row
		# exists so the ratio above is a measurement and not a hypothesis about
		# which core macOS put the audio thread on.
		var calib: PackedInt64Array = _probe.call_calib_ns()
		var calib_offline: Dictionary = _probe.benchmark_int(64)
		if calib.size() > 0 and not calib_offline.is_empty():
			var c_copy: Array[int] = []
			for v in calib:
				c_copy.append(v)
			var c_stats := _int_stats_array(c_copy)
			var c_offline: float = calib_offline["ns_per_rep"]
			_lines.append("")
			_lines.append("    %-32s %12s %12s" % ["integer calibration", "in _mix", "offline"])
			_lines.append("    %-32s %12.0f %12.0f" % [
				"ns for %d dependent int ops" % int(calib_offline["iters"]),
				float(c_stats["median"]), c_offline,
			])
			_lines.append("    %-32s %12.2f %12s" % [
				"audio thread / main thread", float(c_stats["median"]) / maxf(c_offline, 1.0), "—",
			])
			_lines.append("")
			_lines.append("    Both ratios move between runs and they move together. Over eight")
			_lines.append("    consecutive CoreAudio runs the floating-point ratio spanned 3.62 to")
			_lines.append("    6.31 and the integer ratio 1.38 to 2.50, paired — 1.42 with 3.63,")
			_lines.append("    2.50 with 6.28 — so the audio thread lands on cores of at least two")
			_lines.append("    different speeds from run to run. What is stable is the *quotient*:")
			_lines.append("    floating point is consistently about 2.5x worse off than the integer")
			_lines.append("    control on whichever core it got. Budget against the slow end, and")
			_lines.append("    do not quote any single run's ratio as the ratio.")


func _section_transport(report: Dictionary) -> void:
	var facts: Dictionary = _probe.transport_facts(200000)

	_lines.append("")
	_lines.append("=== 6. the state handoff, priced ======================================")
	_lines.append("")
	_lines.append("    sizeof(EngineAudioInput)     %d bytes" % facts["sizeof_engine_audio_input"])
	_lines.append("    alignof                      %d" % facts["alignof_engine_audio_input"])
	_lines.append("    atomic<uint64_t> lock-free   %s" % str(facts["atomic_u64_always_lock_free"]))
	_lines.append("    atomic<payload> lock-free    %s" % str(facts["atomic_payload_always_lock_free"]))
	_lines.append("")
	_lines.append("    seqlock publish              %.1f ns" % facts["seq_publish_ns"])
	_lines.append("    seqlock read                 %.1f ns" % facts["seq_read_ns"])
	_lines.append("")
	_lines.append("    A publish costs %.1f ns once per 120 Hz tick, which is %.6f %% of a" % [
		facts["seq_publish_ns"], 100.0 * facts["seq_publish_ns"] * 120.0 / 1e9,
	])
	_lines.append("    core. A read costs %.1f ns once per mix block, which against the" % facts["seq_read_ns"])
	_lines.append("    %.1f ms budget per second of audio is %.6f %%." % [
		AUDIO_BUDGET_MS * 60.0,
		100.0 * (facts["seq_read_ns"] / 1e9) * (AudioServer.get_mix_rate() / 512.0) * 1000.0 / (AUDIO_BUDGET_MS * 60.0),
	])
	_lines.append("")
	_lines.append("    `atomic<payload>` not being lock-free is the number that rules out")
	_lines.append("    publishing the struct through one: `std::atomic` over an object this")
	_lines.append("    size falls back to a mutex from a global table, taken **inside `_mix`**,")
	_lines.append("    which is precisely what issue #81's third acceptance criterion forbids.")
	_lines.append("")
	_lines.append("    Torn-read counts from section 4 are the empirical half of the same")
	_lines.append("    argument: seqlock %d torn of %d, unsynchronized %d torn of %d." % [
		report["seq_torn"], report["seq_reads"], report["naked_torn"], report["naked_reads"],
	])


# --- small statistics helpers ----------------------------------------------
#
# Kept here rather than in the extension because they are report formatting, and
# a number that only exists to be printed does not belong in a .dylib.


func _int_stats_i32(values: PackedInt32Array) -> Dictionary:
	var copy: Array[int] = []
	for v in values:
		copy.append(v)
	return _int_stats_array(copy)


func _int_stats_i64(values: PackedInt64Array) -> Dictionary:
	var copy: Array[int] = []
	for v in values:
		copy.append(v)
	return _int_stats_array(copy)


func _int_stats_array(values: Array) -> Dictionary:
	if values.is_empty():
		return {"min": 0, "median": 0, "max": 0, "mean": 0.0}
	var sorted := values.duplicate()
	sorted.sort()
	var total := 0.0
	for v in sorted:
		total += float(v)
	return {
		"min": sorted[0],
		"median": sorted[sorted.size() / 2],
		"max": sorted[sorted.size() - 1],
		"mean": total / float(sorted.size()),
	}


func _distinct_i32(values: PackedInt32Array) -> Array:
	var seen := {}
	for v in values:
		seen[v] = true
	var out := seen.keys()
	out.sort()
	return out


func _mean_i64(values: PackedInt64Array) -> float:
	if values.is_empty():
		return 0.0
	var total := 0.0
	for v in values:
		total += float(v)
	return total / float(values.size())
