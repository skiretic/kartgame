extends SceneTree

## What the scrub and wind layers cost on the real audio thread. Issue #84.
##
##     tools/verify/scrub_cost_probe.sh
##     godot --path . --script tools/verify/scrub_cost_probe.gd -- --seconds=2.0
##
## ## Why this runs before the layers are wired into a scene
##
## Issue #152 records the harmonic stack at 69% of §15's audio row at idle and
## **twelve voices at 74.76% of real time**, which is the M7 grid. That is not a
## budget with room in it. Adding a layer per kart to a path already at three
## quarters of its deadline is exactly the kind of change that should be measured
## before it is built, and ADR-0035 is this project's precedent for doing it in
## that order rather than the other one.
##
## ## What is being asked, and what is not
##
## `synth_cost_probe.gd`'s question was whether cost is **linear in partial
## count**, because the partial count is the synth's own decision and swings 6x
## across the rev range. Neither layer here has anything like that. A state
## variable filter is nine multiply-adds per sample whatever its cutoff is, the
## PRNG draw is fixed, and the two libm calls — `std::tan` for the coefficients
## and `std::pow` for the level — are per **block**, not per sample.
##
## So the prediction going in is that cost per frame is **flat** across every
## operating point, and the sweep exists to confirm that rather than to find a
## worst case. A sweep that came back non-flat would itself be the finding, and it
## would mean something in `scrub_wind.h` is doing per-sample work its author did
## not intend — which is precisely the class of thing this project has been wrong
## about before.
##
## The second question is attribution, and it is why the modes are separate:
##
##   * **scrub only** is what an opponent's kart costs. Positional, one per kart,
##     twelve of them at M7.
##   * **scrub + wind** is what the player's kart costs. The wind layer is
##     non-positional and there is exactly one of it, ever.
##
## Summing the two into one number would make the twelve-kart column wrong by
## eleven wind layers that do not exist.
##
## ## Why CoreAudio only
##
## ADR-0035's second surprise, unchanged: the headless Dummy driver calls `_mix`
## in bursts, runs its clock at 0.967 of wall-clock, and schedules the mixer on an
## ordinary thread where the cost ratio is 1.0 instead of 3.6-6.3. **Any cost
## figure taken under `--headless` is wrong in scale and in shape.** This probe
## refuses to report one rather than printing a number that would later be quoted
## by somebody who did not read this paragraph.

## Operating points. `[drive, speed_ms, surface]`.
##
## Chosen to exercise every branch in `ScrubSynth::render` and `WindSynth::render`
## rather than to be round numbers:
##
##   * silent — the drive is zero, so `level_` decays to zero and the samples are
##     zero, but every filter and PRNG call still runs. If cost here is lower than
##     the rest, something is short-circuiting and the layer would be cheap
##     exactly when nobody is listening and expensive when they are;
##   * a slow hairpin at full slip, below the speed ramp's saturation;
##   * a fast corner at full slip on asphalt, which is the loudest the layer gets;
##   * the same on grass, where the band moves and widens — a different filter
##     coefficient set, and the check that the coefficient update is not where the
##     cost is; and
##   * the end of the straight, 38 m/s, where the wind layer is at unity and its
##     low-pass corner is highest.
const SWEEP: Array = [
	["silent", 0.0, 0.0, 0],
	["hairpin", 1.0, 6.0, 0],
	["fast corner asphalt", 1.0, 30.0, 0],
	["fast corner grass", 1.0, 30.0, 2],
	["straight", 0.1, 38.0, 0],
]

## Seconds measured per operating point, after settling. At a 512-frame block and
## 48 kHz that is about 94 `_mix` calls per second, so 2 s is ~188 blocks.
const DEFAULT_SECONDS := 2.0

## Seconds allowed before the counters are armed. The scrub layer's drive one-pole
## has a ~35 ms time constant, so half a second is ~14 of them — the level has
## arrived and the arithmetic is the steady-state arithmetic.
const SETTLE_S := 0.5

## §15's row, printed for the same reason `synth_cost_probe.gd` prints it and with
## the same warning: it is main-thread time in a rendered frame and the audio
## thread spends none of it. Issue #155.
const AUDIO_BUDGET_MS := 0.5
const FRAME_MS := 1000.0 / 60.0

## `ARCHITECTURE.md` §7's grid size, for the column that matters.
const M7_KARTS := 12

## What one engine voice costs at its worst, from `synth_cost_probe.gd` and quoted
## in `engine_voice.h`: 1412.8 ns/frame at a 2,000-2,500 rpm idle, 191 partials.
##
## Copied here **only to express the new layers as a fraction of it**, and that is
## a comparison rather than a second owner of the number: if it drifts, the ratio
## printed below is wrong by however much it drifted and the absolute figures this
## probe measures are unaffected.
const ENGINE_VOICE_NS_PER_FRAME := 1412.8

var _probe: Object
var _player: AudioStreamPlayer
var _seconds := DEFAULT_SECONDS

var _cell := 0
var _pass := 0 # 0 = scrub only, 1 = scrub + wind
var _phase := "settle"
var _phase_ticks := 0
var _rows: Array[Array] = []
var _lines: Array[String] = []
var _started := false
var _mix_rate := 48000.0
var _tick_hz := 120.0


func _initialize() -> void:
	var args := Cmdline.parse()
	_seconds = maxf(Cmdline.as_float(args, "seconds", DEFAULT_SECONDS), 0.25)

	for required in ["AudioProbe", "AudioProbeStream", "AudioProbePlayback"]:
		if not ClassDB.class_exists(required):
			printerr("error: %s is not registered — scons target=editor arch=arm64" % required)
			quit(1)
			return

	var driver := AudioServer.get_driver_name()
	if driver == "Dummy":
		printerr("error: the audio driver is Dummy. Cost measured under it is wrong in")
		printerr("       scale and in shape — ADR-0035's second surprise. Run without")
		printerr("       --headless, or with --audio-driver=CoreAudio.")
		quit(1)
		return

	_mix_rate = AudioServer.get_mix_rate()
	_tick_hz = float(Engine.physics_ticks_per_second)

	_probe = ClassDB.instantiate("AudioProbe")
	_probe.set_mix_rate(_mix_rate)
	# Both of the other render paths off. `set_scrub_wind` is checked first inside
	# `_mix`, so neither would be reached — but a stale partial count or a stale rpm
	# sitting in the report beside these figures is an invitation to read the wrong
	# one, which is the mistake `synth_cost_probe.gd` guards the same way.
	_probe.set_engine_synth(false)
	_probe.set_partials(0)
	_probe.set_physics_busy_us(0)
	_probe.set_scrub_wind(true, false)
	_apply_cell()
	get_root().add_child(_probe)

	var stream: AudioStream = ClassDB.instantiate("AudioProbeStream")
	_player = AudioStreamPlayer.new()
	_player.stream = stream
	# Silent at the player, loud inside the synth. ADR-0035 answer 2 confirmed Godot
	# does not skip `_mix` for a quiet player, so the room stays quiet and the
	# arithmetic still runs at the level it will really run at — a denormal-flushing
	# zero would be a faster filter than the one being shipped.
	_player.volume_db = -80.0
	get_root().add_child(_player)


func _apply_cell() -> void:
	var cell: Array = SWEEP[_cell]
	_probe.set_scrub_wind_operating_point(float(cell[1]), float(cell[2]), int(cell[3]))
	_probe.set_scrub_wind(true, _pass == 1)


func _physics_process(_delta: float) -> bool:
	if not _started:
		_started = true
		_player.play()
		return false

	_phase_ticks += 1

	if _phase == "settle":
		if _phase_ticks >= int(round(SETTLE_S * _tick_hz)):
			_probe.arm()
			_phase = "measure"
			_phase_ticks = 0
		return false

	if _phase_ticks < int(round(_seconds * _tick_hz)):
		return false

	_record()
	_cell += 1
	if _cell >= SWEEP.size():
		_cell = 0
		_pass += 1
		if _pass > 1:
			_report()
			quit(0)
			return true

	_apply_cell()
	_phase = "settle"
	_phase_ticks = 0
	return false


## One operating point's counters, reduced to the numbers a dropout depends on:
## the typical block, the worst block, and how many blocks there were to be worst
## out of. Same reduction as `synth_cost_probe.gd`, minus the partial count, which
## does not exist here.
func _record() -> void:
	var cell: Array = SWEEP[_cell]
	var layer_ns: PackedInt64Array = _probe.call_synth_ns()
	var frames: PackedInt32Array = _probe.call_frames()

	if layer_ns.is_empty():
		_rows.append([_pass, cell[0], 0.0, 0.0, 0])
		return

	# Sorted for a median. The mean is not the headline: one scheduling stall
	# inflates it and hides the typical cost, and the maximum is reported separately
	# because that is the block that actually underruns.
	var sorted: Array[int] = []
	for value in layer_ns:
		sorted.append(value)
	sorted.sort()

	var median_ns := float(sorted[sorted.size() / 2])
	var max_ns := float(sorted[sorted.size() - 1])
	var block_frames := int(frames[0]) if not frames.is_empty() else 512

	_rows.append([
		_pass, cell[0],
		median_ns / float(block_frames),
		max_ns / float(block_frames),
		sorted.size()])


func _report() -> void:
	var frame_period_ns := 1.0e9 / _mix_rate

	_lines.append("")
	_lines.append("=== ScrubSynth and WindSynth on the real audio thread ===")
	_lines.append("")
	_lines.append("    audio driver                 %s" % AudioServer.get_driver_name())
	_lines.append("    mix rate                     %.1f Hz" % _mix_rate)
	_lines.append("    frame period                 %.1f ns" % frame_period_ns)
	_lines.append("    measured per point           %.2f s after %.2f s settle" % [_seconds, SETTLE_S])
	_lines.append("")

	for pass_index in [0, 1]:
		var title := "scrub only — what one opponent's kart costs" if pass_index == 0 \
			else "scrub + wind — what the player's kart costs"
		_lines.append("    %s" % title)
		_lines.append("")
		_lines.append("    %-22s %10s %9s %9s %8s" % [
			"operating point", "ns/frame", "% real", "worst %", "blocks"])
		for row in _rows:
			if int(row[0]) != pass_index:
				continue
			_lines.append("    %-22s %10.1f %9.3f %9.3f %8d" % [
				row[1], row[2],
				100.0 * float(row[2]) / frame_period_ns,
				100.0 * float(row[3]) / frame_period_ns,
				row[4]])
		_lines.append("")

	# The flatness check. This is the prediction the header states, and it is the
	# only model in this file — so it is printed as a spread rather than asserted.
	# A layer whose cost depends on its operating point is doing per-sample work
	# somebody meant to do per block.
	for pass_index in [0, 1]:
		var lo := 1.0e30
		var hi := 0.0
		for row in _rows:
			if int(row[0]) != pass_index:
				continue
			lo = minf(lo, float(row[2]))
			hi = maxf(hi, float(row[2]))
		var label := "scrub only" if pass_index == 0 else "scrub + wind"
		_lines.append("    %-14s ns/frame across every operating point: %.1f to %.1f, spread %.1f%%"
			% [label, lo, hi, 100.0 * (hi - lo) / maxf(lo, 1e-9)])
	_lines.append("")
	_lines.append("    The prediction was flat: no partial count, no per-sample libm, and")
	_lines.append("    the coefficient update is once per block. A large spread here would")
	_lines.append("    mean scrub_wind.h is doing per-sample work it did not intend to.")
	_lines.append("")

	# The budget, which is the reason the probe exists. #152's twelve-voice figure
	# is what this lands on top of.
	var scrub_worst := 0.0
	var both_worst := 0.0
	for row in _rows:
		if int(row[0]) == 0:
			scrub_worst = maxf(scrub_worst, float(row[2]))
		else:
			both_worst = maxf(both_worst, float(row[2]))
	var wind_only := both_worst - scrub_worst

	_lines.append("    against the M7 budget #152 already records as tight")
	_lines.append("")
	_lines.append("      %-46s %8.1f ns/frame" % ["one engine voice, worst (quoted)", ENGINE_VOICE_NS_PER_FRAME])
	_lines.append("      %-46s %8.1f ns/frame" % ["one scrub layer, worst", scrub_worst])
	_lines.append("      %-46s %8.1f ns/frame" % ["the one wind layer, by difference", wind_only])
	_lines.append("      %-46s %8.2f %%" % [
		"scrub as a fraction of an engine voice",
		100.0 * scrub_worst / ENGINE_VOICE_NS_PER_FRAME])
	_lines.append("")
	_lines.append("      %-46s %8.3f %%" % [
		"of real time, player's kart (scrub + wind)", 100.0 * both_worst / frame_period_ns])
	_lines.append("      %-46s %8.3f %%" % [
		"of real time, %d scrub layers (M7)" % M7_KARTS,
		100.0 * scrub_worst * float(M7_KARTS) / frame_period_ns])
	_lines.append("      %-46s %8.3f %%" % [
		"of §15's 0.5 ms/frame row (#155: wrong denom.)",
		100.0 * both_worst / frame_period_ns / (AUDIO_BUDGET_MS / FRAME_MS)])
	_lines.append("")
	_lines.append("    §15's row is main-thread time in a rendered frame and the audio")
	_lines.append("    thread spends none of it. The percentage above it is the one that")
	_lines.append("    starves the device. Issue #155 restates the row.")
	_lines.append("")

	for line in _lines:
		print(line)
