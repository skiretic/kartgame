extends SceneTree

## What `EngineSynth::render` costs on the real audio thread, against rpm.
##
##     tools/verify/synth_cost_probe.sh
##     godot --path . --script tools/verify/synth_cost_probe.gd -- --seconds=2.0
##
## ## The question, and why ADR-0035 does not already answer it
##
## ADR-0035 answer 5 measured a 24-partial `std::sin` stack at 576-1002 ns/frame
## inside `_mix`, and 3.6x-6.3x that of the same code on the main thread. That
## settled the boundary. It does not settle the cost of *this* synth, for two
## reasons that pull in opposite directions and so cannot be waved through:
##
## **The stand-in was cheaper per partial.** It is a phase accumulator and a gain.
## `EngineSynth::render` crossfades two ladders, ramps a gain per partial per
## block, runs a comb, a noise layer and combustion jitter, and reads an
## interpolated table rather than calling `std::sin`.
##
## **The partial count is not 24 and is not the caller's choice.**
## `EngineSynth::active_partials` fills to a *frequency* ceiling —
## `min(STACK_CEILING_HZ, MAX_PARTIALS * f0)` — so it returns **192 at a 2,000 rpm
## idle and 40 at 12,000 rpm**. Idle is the worst case, by 4.8x over the top of the
## rev range, and by 8x over the count ADR-0035 costed. `kz_audio_reference.h`
## explains at length why a fixed index would be worse: a brick wall that slides
## with rpm is audible as the note changing character across the range, which is
## #82's own acceptance criterion failing for a reason unrelated to stepping.
##
## So the interesting number is not one cost. **It is whether cost is linear in
## partial count**, because linearity is the assumption under any extrapolation
## from a 24-partial measurement, and issue #107 is this project's worked example
## of a conclusion resting on a model instead of a measurement. This probe sweeps
## rpm, reads back the count the synth chose, and prints the residual against a
## line fitted through the cheapest and most expensive cells. If the residual is
## small the extrapolation was fair; if it is not, the shape is the finding.
##
## ## Why the denominator is the block deadline and not section 15
##
## `ARCHITECTURE.md` §15 gives audio 0.5 ms of a 16.6 ms rendered frame. Issue
## [#155](https://github.com/skiretic/kartgame/issues/155) exists because that row
## is specified against main-thread time in a rendered frame and **the audio thread
## spends none of it** — it runs on its own core, on its own deadline, and a frame
## that takes 40 ms does not give the mixer 40 ms.
##
## The deadline that actually produces a dropout is the block period: 512 frames
## at 48 kHz is 10.67 ms of audio that must be synthesized in less than 10.67 ms of
## wall-clock. So the primary column here is the fraction of real time consumed,
## which is unambiguous and is what starves the device. §15's figure is printed
## beside it, labeled, so the two are not confused — and so that whoever restates
## the row for #155 has the measurement to restate it against.
##
## ## Why CoreAudio only
##
## ADR-0035's second surprise: the headless Dummy driver calls `_mix` in bursts
## (median interval 166.9 us against a deadline of 11,610), runs its clock at 0.967
## of wall-clock, and schedules the mixer on an ordinary thread where the cost ratio
## is 1.0 instead of 3.6-6.3. **Any cost figure taken under `--headless` is wrong in
## scale and in shape.** This probe therefore refuses to report one, rather than
## printing a number that would be quoted later by someone who did not read this
## paragraph.

## Operating points, rpm. Chosen to bracket the partial count rather than to be
## round numbers.
##
## 2,000 is `Engine::idle_rpm` and is where the 192-partial cap binds — the worst
## case, and the one a driver sits in while stationary in the pits, which is
## exactly when a dropout is most obvious because nothing else is making noise.
## 2,500 is where `engine_synth.h`'s own comment says the cap stops binding. 14,300
## is `Engine::soft_cut_rpm`. The rest fill the range.
const RPM_SWEEP: Array[float] = [2000.0, 2500.0, 3500.0, 5000.0, 7000.0, 9000.0, 12000.0, 14300.0]

## Seconds measured per operating point, after settling. At a 512-frame block and
## 48 kHz that is about 94 `_mix` calls per second, so 2 s is ~188 blocks — enough
## for a median to mean something and for the maximum to have seen a scheduling
## hiccup or two.
const DEFAULT_SECONDS := 2.0

## Seconds allowed for the synth's control smoothing to reach the new rpm before
## the counters are armed. `synth_tuning::CONTROL_SMOOTH_S` is a one-pole, so a
## measurement started immediately would average the ramp — and the partial count
## changes *during* the ramp, which would put two different denominators in one
## cell.
const SETTLE_S := 0.5

## Load and throttle state held through the sweep. On the pipe at full load, which
## is the expensive ladder: `ONPIPE_LADDER_DB` is the flattest one measured, so the
## top of the stack carries real gain rather than being faded out by the ceiling
## gate, and every partial is doing arithmetic that matters.
const SWEEP_LOAD := 1.0
const SWEEP_TRAILING := false

## §15's row, for the comparison this probe exists to make honest. Not the
## denominator of the primary column. See the header.
const AUDIO_BUDGET_MS := 0.5
const FRAME_MS := 1000.0 / 60.0

## Independent engine voices M7 needs, for the "and then twelve of them" column.
## `ARCHITECTURE.md` §7's grid size.
const M7_KARTS := 12

var _probe: Object
var _player: AudioStreamPlayer
var _seconds := DEFAULT_SECONDS

var _cell := 0
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

	# The one refusal in this file, and it is the whole reason ADR-0035's second
	# surprise was written down. A Dummy-driver cost figure is wrong in scale and
	# in shape, and printing one with a caveat is how it gets quoted without the
	# caveat six months later.
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
	_probe.set_engine_synth(true)
	# The stand-in stack is switched off rather than left at its default: with
	# `use_engine_synth` set it is not reached, but a partial count of 24 sitting in
	# the report next to a synth count of 192 is an invitation to read the wrong one.
	_probe.set_partials(0)
	# No physics busy window. That existed to answer ADR-0035's overlap question and
	# its microsecond spin would land inside the cost being measured here.
	_probe.set_physics_busy_us(0)
	_probe.set_synth_operating_point(RPM_SWEEP[0], SWEEP_LOAD, SWEEP_TRAILING)
	get_root().add_child(_probe)

	var stream: AudioStream = ClassDB.instantiate("AudioProbeStream")
	_player = AudioStreamPlayer.new()
	_player.stream = stream
	# Silent, and mixed anyway: ADR-0035 answer 2 confirmed Godot does not skip
	# `_mix` for a quiet player, so the room stays quiet and the measurement does
	# not change. `EngineSynth`'s own gain is untouched — its arithmetic must run at
	# the level it will really run at, because a denormal-flushing zero would be a
	# faster synth than the one being shipped.
	_player.volume_db = -80.0
	get_root().add_child(_player)


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

	_record(RPM_SWEEP[_cell])
	_cell += 1
	if _cell >= RPM_SWEEP.size():
		_report()
		quit(0)
		return true

	_probe.set_synth_operating_point(RPM_SWEEP[_cell], SWEEP_LOAD, SWEEP_TRAILING)
	_phase = "settle"
	_phase_ticks = 0
	return false


## One operating point's worth of counters, reduced to the four numbers a dropout
## depends on: how many partials were paid for, the typical block, the worst block,
## and how many blocks there were to be worst out of.
func _record(rpm: float) -> void:
	var report: Dictionary = _probe.report()
	var synth_ns: PackedInt64Array = _probe.call_synth_ns()
	var frames: PackedInt32Array = _probe.call_frames()

	if synth_ns.is_empty():
		_rows.append([rpm, 0, 0.0, 0.0, 0, 0.0])
		return

	# Sorted for a median. The mean is deliberately not the headline: a single
	# scheduling stall inflates it and hides the typical cost, while the maximum is
	# reported separately because that is the block that actually underruns.
	var sorted: Array[int] = []
	for value in synth_ns:
		sorted.append(value)
	sorted.sort()

	var median_ns := float(sorted[sorted.size() / 2])
	var max_ns := float(sorted[sorted.size() - 1])
	var block_frames := int(frames[0]) if not frames.is_empty() else 512
	var partials := int(report["synth_partials"])

	# Per frame rather than per block, because the block size is the device's
	# choice and the frame period is the deadline's unit.
	var per_frame_ns := median_ns / float(block_frames)
	_rows.append([rpm, partials, per_frame_ns, max_ns / float(block_frames), sorted.size(), median_ns])


func _report() -> void:
	var frame_period_ns := 1.0e9 / _mix_rate

	_lines.append("")
	_lines.append("=== EngineSynth::render on the real audio thread ===")
	_lines.append("")
	_lines.append("    audio driver                 %s" % AudioServer.get_driver_name())
	_lines.append("    mix rate                     %.1f Hz" % _mix_rate)
	_lines.append("    frame period                 %.1f ns" % frame_period_ns)
	_lines.append("    measured per point           %.2f s after %.2f s settle" % [_seconds, SETTLE_S])
	_lines.append("    load / trailing              %.2f / %s" % [SWEEP_LOAD, SWEEP_TRAILING])
	_lines.append("")

	# The primary table. "% real" is the number that causes or does not cause a
	# dropout; everything else is context for it.
	_lines.append("    %7s %8s %8s %10s %9s %9s %8s" % [
		"rpm", "f0 Hz", "partials", "ns/frame", "% real", "worst %", "blocks"])
	for row in _rows:
		var rpm: float = row[0]
		var partials: int = row[1]
		var per_frame: float = row[2]
		var worst_per_frame: float = row[3]
		var blocks: int = row[4]
		_lines.append("    %7.0f %8.1f %8d %10.1f %9.2f %9.2f %8d" % [
			rpm, rpm / 60.0, partials, per_frame,
			100.0 * per_frame / frame_period_ns,
			100.0 * worst_per_frame / frame_period_ns,
			blocks])

	# The model under test. A line through the cheapest and most expensive cells,
	# in partial count — which is precisely the extrapolation an 8x-of-24-partials
	# argument makes — and the residual of every other cell against it. Small
	# residuals mean the extrapolation was fair and the cost is per-partial work.
	# Large ones mean there is a fixed cost per block, or a cost that is not in the
	# partial count at all, and that shape is the finding.
	var lo := _rows[0]
	var hi := _rows[0]
	for row in _rows:
		if row[1] < lo[1]:
			lo = row
		if row[1] > hi[1]:
			hi = row

	_lines.append("")
	_lines.append("    is cost linear in partial count? A line through the extremes, and")
	_lines.append("    the residual of every cell against it. This is the assumption any")
	_lines.append("    extrapolation from ADR-0035's 24 partials rests on.")
	_lines.append("")
	if int(hi[1]) == int(lo[1]):
		_lines.append("      the sweep produced one partial count — no line to fit")
	else:
		var slope: float = (float(hi[2]) - float(lo[2])) / (float(hi[1]) - float(lo[1]))
		var intercept: float = float(lo[2]) - slope * float(lo[1])
		_lines.append("      ns/frame = %.3f * partials + %.1f" % [slope, intercept])
		_lines.append("      %.3f ns per partial per frame, %.1f ns/frame of fixed cost"
			% [slope, intercept])
		_lines.append("")
		_lines.append("      %8s %10s %10s %10s" % ["partials", "measured", "line", "residual"])
		var worst_residual := 0.0
		for row in _rows:
			var predicted: float = slope * float(row[1]) + intercept
			var residual: float = float(row[2]) - predicted
			if absf(residual) > absf(worst_residual):
				worst_residual = residual
			_lines.append("      %8d %10.1f %10.1f %+10.1f" % [row[1], row[2], predicted, residual])
		var span: float = float(hi[2]) - float(lo[2])
		_lines.append("")
		_lines.append("      worst residual %+.1f ns/frame, %.1f%% of the swept range"
			% [worst_residual, 100.0 * absf(worst_residual) / maxf(span, 1e-9)])

	# The two denominators, side by side and labeled, because the whole reason #155
	# exists is that they were conflated once already.
	var worst_cell := _rows[0]
	for row in _rows:
		if float(row[2]) > float(worst_cell[2]):
			worst_cell = row
	var worst_frac: float = float(worst_cell[2]) / frame_period_ns

	_lines.append("")
	_lines.append("    the worst operating point, against both denominators")
	_lines.append("")
	_lines.append("      %-46s %6.0f rpm, %d partials" % [
		"worst cell", float(worst_cell[0]), int(worst_cell[1])])
	_lines.append("      %-46s %8.2f %%" % [
		"of real time, one kart  <- the deadline", 100.0 * worst_frac])
	_lines.append("      %-46s %8.2f %%" % [
		"of real time, %d karts (M7)" % M7_KARTS, 100.0 * worst_frac * float(M7_KARTS)])
	# The §15 comparison. One kart's synth spends `worst_frac` of every second; §15
	# allots 0.5 ms per 16.6 ms frame, which is `AUDIO_BUDGET_MS / FRAME_MS` of a
	# second of main-thread time. Dividing one by the other is the arithmetic #155
	# has to either endorse or replace, and it is printed rather than performed in
	# somebody's head.
	_lines.append("      %-46s %8.2f %%" % [
		"of §15's 0.5 ms/frame row (#155: wrong denom.)",
		100.0 * worst_frac / (AUDIO_BUDGET_MS / FRAME_MS)])
	_lines.append("")
	_lines.append("    §15's row is main-thread time in a rendered frame and the audio")
	_lines.append("    thread spends none of it. The percentage above it is the one that")
	_lines.append("    starves the device. Issue #155 restates the row; this is the")
	_lines.append("    measurement to restate it against.")
	_lines.append("")

	for line in _lines:
		print(line)
