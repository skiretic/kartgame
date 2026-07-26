extends SceneTree

## What Godot's 3D audio path costs in level, and how loud the shipped rig
## actually is. Issue #160.
##
##     godot --display-driver headless --audio-driver CoreAudio \
##         --path . --script tools/verify/audio_level_probe.gd
##
## Arguments, all after a bare `--`:
##
##     --seconds=N      measured window per cell (0.35)
##     --settle=N       seconds discarded before each window (0.25)
##     --skip-rig       sections 0-3 only, no `KartBody`
##
## ## Why this file exists
##
## The driver's report on #160 is not "the wind is too loud relative to the
## engine". It is "I have to turn the system volume up so high to hear the engine
## that the wind's floor comes up with it". Those are different defects and only
## one of them is fixed by moving a gain: the first is a ratio, the second is an
## absolute level, and `e7b0eaa` tuned the ratio because nobody had the absolute
## number. This probe is the absolute number.
##
## **ADR-0035 put "Godot's own mixing, bussing and 3D attenuation" in its "what
## this does not settle" list** and nothing has measured it since. Meanwhile
## `engine_voice_rig.gd` sets `UNIT_SIZE = 3.0` with a comment saying it is guessed
## and should be measured before anyone calls it done, and `scripts/game/` has no
## `AudioListener3D` anywhere -- so the listener is whatever Godot falls back to,
## which is an engine behavior this project has a standing rule against assuming.
## CLAUDE.md's trap list is four entries of assumed-engine-behavior that measured
## the other way, and ADR-0033 alone contradicted three things already written
## down as settled.
##
## So: one question per section, an analytic prediction printed beside every
## measurement, exactly as `integration_probe.gd` and `contact_probe.gd` do it.
##
## ## The five questions
##
##   0. **Is the capture path honest?** A non-positional player rendering a
##      full-scale sine must read exactly 0.00 dBFS. Everything below is a
##      difference against that, so a capture path wrong by a factor has to be
##      excluded before any of it means anything. This section is the calibration
##      and it is first for that reason.
##
##   1. **What does the distance law cost, in dB, at the distances that occur?**
##      Godot's `ATTENUATION_INVERSE_DISTANCE` is documented as a gain of
##      `unit_size / distance`, offset by `volume_db` and clamped above at
##      `max_db`. That is a prediction with three named parameters, and every one
##      of them is printed beside what came out.
##
##   2. **Where is the listener?** Move the camera with the emitter fixed. If the
##      level changes, the listener is the camera. Then make an explicit
##      `AudioListener3D` current at the emitter and move the camera again: if the
##      level stops changing, the listener is overridable and the fix for a
##      camera-dependent mix is one node. Both halves are needed -- the first
##      alone cannot tell you what to do about it.
##
##   3. **Is the attenuation low-pass doing anything?** `AudioStreamPlayer3D`
##      carries `attenuation_filter_cutoff_hz`, and `engine_voice_rig.gd` never
##      sets it. If its default is below the top of the harmonic stack then every
##      3D emitter in this project has been quietly low-passed since the note was
##      written, which would read to a driver as "quiet and dull" rather than as a
##      filter. **Swept with sines**, one frequency per cell, because CLAUDE.md's
##      trap list records that estimating a filter's response from one pass of
##      noise put a 900 Hz band at 776 Hz and a Q of 2.4 at 10.2.
##
##   4. **How loud is the shipped rig, in dBFS?** Sections 0-3 use a reference
##      signal of exactly known amplitude so that the transfer function is
##      separable from the synth. Section 4 then runs the real thing --
##      `EngineVoiceRig` on a real `KartBody`, at the shipped `VOICE_GAIN`,
##      `SCRUB_GAIN` and `WIND_GAIN` -- and reports peak and RMS against full
##      scale. That number is what says whether the honest fix is at the player,
##      at a bus, or inside the synth, which is the one thing #160 says has to be
##      decided rather than measured.
##
## ## Two channels, and why the report carries both
##
## An `AudioStreamPlayer3D` **pans** a mono source, and a source directly in front
## of the listener is centered, which under a constant-power pan is 1/sqrt(2) into
## each channel -- 3.01 dB down per channel, with the total power unchanged. A
## plain `AudioStreamPlayer` writes the full amplitude into **both** channels.
##
## So a positional emitter and a non-positional one at the same nominal gain do
## not arrive at the same level, and the difference is exactly 3 dB. That is not a
## curiosity here: the engine note and the scrub are `AudioStreamPlayer3D` and the
## wind is a plain `AudioStreamPlayer` (ADR-0038 section 4), so **the wind layer
## collects a free 3 dB over the other two for no reason a driver would guess.**
## Reporting one channel would hide it and reporting only the total would hide
## which channel it is in, so both are printed.
##
## `total` is `10*log10(mean(L^2) + mean(R^2))`, normalized so that a full-scale
## sine present in both channels reads 0.00 dB. That makes section 0 a literal
## zero and every other row a difference against it.
##
## ## Why a reference signal rather than the synth, for sections 0-3
##
## `AudioProbeStream` with `partials = 1` renders `g_partial_gain[0] * sin(phase)`
## scaled by `gain`, and `partial_gain(1)` is `1^k`, which is exactly 1.0 for any
## exponent. So one partial at `gain = 1.0` is a full-scale sine and nothing about
## the measurement depends on the synth's own level being understood first. That
## matters because the synth's level is the *other* half of the question and is
## measured separately in pure C++ -- mixing the two would leave a single number
## that could be blamed on either.
##
## ## Why CoreAudio only
##
## Less absolute than `scrub_cost_probe.gd`'s refusal, and for a different reason.
## Cost figures taken under the Dummy driver are wrong by 6x (ADR-0035). *Level*
## figures probably are not -- the arithmetic is the same arithmetic -- but the
## Dummy driver calls `_mix` in irregular bursts, and a capture buffer drained on
## a physics tick against a mixer that arrives in bursts is a sampling problem
## nobody has characterized. Refusing is cheaper than characterizing it.

## Distances the listener is parked at, meters. Not round numbers, and each one is
## a place a listener genuinely is:
##
##   * **0.52** -- the driver's head to the engine mount. `chassis.h`'s lump table
##     puts the head at (0.000, 0.560, 0.128) and the engine at
##     (0.319, 0.150, 0.190), which is 0.523 m apart. This is the cockpit case and
##     it is *inside* `UNIT_SIZE`, where an inverse law predicts gain above unity.
##   * **3.56** -- the chase camera. `chase_camera.gd` has `ARM_LENGTH = 3.4` and
##     `ARM_HEIGHT = 1.05`, so the camera sits 3.56 m from the kart origin. This
##     is the only distance the current mix has ever been judged at.
##   * **3.00** -- `UNIT_SIZE` itself, where the law predicts exactly unity.
##   * **150.0** -- `MAX_DISTANCE`, and 200 past it. What happens at and beyond the
##     cutoff is a separate behavior from the law and is not assumed to be
##     continuous with it.
##
## The rest fill in the curve so that a law of the wrong *shape* is visible rather
## than being absorbed into a constant offset.
const DISTANCES: Array[float] = [
	0.52, 1.00, 2.00, 3.00, 3.56, 6.00, 10.00, 20.00, 50.00, 100.00, 150.00, 200.00,
]

## Distances the listener question is asked at. Four is enough to tell a swing
## from a constant, and this section is a yes-or-no.
const LISTENER_DISTANCES: Array[float] = [1.00, 3.56, 10.00, 20.00]

## Frequencies the attenuation filter is swept at, Hz.
##
## Spanning the harmonic stack rather than the audible range. A two-stroke fires
## once per revolution, so at 10,000 rpm the fundamental is 166.7 Hz and the stack
## runs up to `kz_audio::STACK_CEILING_HZ`. A low-pass anywhere in the top half of
## that span takes the bite out of the note without making it obviously filtered,
## which is the failure this is looking for.
const FILTER_HZ: Array[float] = [
	125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0,
]

## Distances the filter sweep is run at, meters. Two of them, because the whole
## question is whether the filter is distance-dependent: Godot's own docs describe
## the attenuation filter as applied with distance, and one distance cannot tell a
## fixed low-pass from a distance-scaled one. The chase camera's distance is the
## one that matters and 20 m is far enough to move it if it moves.
const FILTER_DISTANCES: Array[float] = [3.56, 20.00]

## Seconds measured per cell, and seconds discarded first.
##
## The settle window exists because moving the listener moves it through Godot's
## own per-block interpolation, and because a player that was just told to play
## has a block or two of ramp. 0.25 s at a 512-frame block and 44.1 kHz is about
## 21 blocks, a long way past any of it.
const DEFAULT_SECONDS := 0.35
const DEFAULT_SETTLE := 0.25

## The reference signal. One partial at unity is a full-scale sine -- see the
## header -- and 500 Hz is well inside every filter and every device.
const REFERENCE_HZ := 500.0
const REFERENCE_GAIN := 1.0

## Section 4's engine operating point is the solver's own, not injected, for the
## reason `voice_probe.gd` gives: publishing a synthetic rpm would measure the
## seqlock while skipping `KartBody::publish_engine_audio`. Full throttle from an
## engaged gear, and the rpm that results is printed rather than assumed.
const RIG_THROTTLE := 1.0

## The scrub and wind operating point section 4 measures at: full slip drive, a
## fast corner at 30 m/s, on asphalt. The same cell `scrub_cost_probe.gd` calls
## "fast corner asphalt", so the level figure here and the cost figure there are
## about the same operating point rather than two different ones.
const SCRUB_DRIVE := 1.0
const SCRUB_SPEED_MS := 30.0
const SCRUB_SURFACE := 0

## Bus the measurement is taken on. Its own bus rather than Master, because the
## capture has to sit somewhere that is not also carrying whatever else the scene
## makes, and because Master's own volume can then be pulled down to keep the room
## quiet without changing what is captured. Section 0 is what proves that claim.
const PROBE_BUS := "Probe"

## Master trim while the probe runs, dB. The capture is upstream of it, so this
## changes what a person in the room hears and nothing that is reported.
const MASTER_TRIM_DB := -60.0

var _seconds := DEFAULT_SECONDS
var _settle := DEFAULT_SETTLE
var _skip_rig := false

var _capture: AudioEffectCapture
var _camera: Camera3D
var _listener: AudioListener3D

var _probe: Object
var _flat_player: AudioStreamPlayer
var _spatial_player: AudioStreamPlayer3D
var _scrub_reference: AudioStreamPlayer3D

var _kart: KartBody
var _engine_stream: Object
var _engine_player: AudioStreamPlayer3D
var _scrub_player: AudioStreamPlayer3D
var _wind_player: AudioStreamPlayer

## Camera offset held against the kart while section 4 runs. The kart has no
## collision shape and falls, so a fixed camera would be a moving distance and
## every row would be a different cell.
var _follow_kart := false
var _follow_offset := Vector3.ZERO

var _cells: Array = []
var _cell := 0
var _phase := "settle"
var _phase_ticks := 0
var _started := false

var _sum_left := 0.0
var _sum_right := 0.0
var _frames := 0
var _peak := 0.0

var _rows: Array = []
var _tick_hz := 120.0
var _mix_rate := 48000.0
var _default_cutoff_hz := 5000.0
var _default_filter_db := 0.0
var _rig_note := ""


func _initialize() -> void:
	var args := Cmdline.parse()
	_seconds = maxf(Cmdline.as_float(args, "seconds", DEFAULT_SECONDS), 0.10)
	_settle = maxf(Cmdline.as_float(args, "settle", DEFAULT_SETTLE), 0.05)
	_skip_rig = Cmdline.as_bool(args, "skip-rig", false)

	if AudioServer.get_driver_name() == "Dummy":
		printerr("error: the audio driver is Dummy. This probe drains a capture buffer on")
		printerr("       a physics tick, and the Dummy driver delivers _mix in irregular")
		printerr("       bursts (ADR-0035), so the sampling is uncharacterized. Run with")
		printerr("       --display-driver headless --audio-driver CoreAudio.")
		quit(1)
		return

	var required := ["AudioProbe", "AudioProbeStream", "AudioProbePlayback"]
	if not _skip_rig:
		required.append_array(["KartBody", "EngineVoiceStream", "NoiseVoiceStream"])
	for name in required:
		if not ClassDB.class_exists(name):
			printerr("error: %s is not registered - scons target=editor arch=arm64" % name)
			quit(1)
			return

	_mix_rate = AudioServer.get_mix_rate()
	_tick_hz = float(Engine.physics_ticks_per_second)

	_build_bus()
	_build_listener()
	_build_reference()
	if not _skip_rig:
		_build_rig()
	_build_cells()


## A dedicated bus with an `AudioEffectCapture` on it, and Master pulled down.
##
## The capture sits on the probe bus, which is upstream of Master's volume, so
## trimming Master keeps the room quiet without touching a single number in the
## report. Section 0 is the check on that: a trim that were being captured would
## move the calibration row off zero by exactly 60 dB, which is not a subtle
## failure to spot.
func _build_bus() -> void:
	var index := AudioServer.bus_count
	AudioServer.add_bus(index)
	AudioServer.set_bus_name(index, PROBE_BUS)
	AudioServer.set_bus_send(index, "Master")
	_capture = AudioEffectCapture.new()
	# Long enough that a physics tick's worth of frames cannot overflow it. At
	# 120 Hz a tick is 368 frames at 44.1 kHz; a 0.5 s ring is 22,050.
	_capture.buffer_length = 0.5
	AudioServer.add_bus_effect(index, _capture)
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("Master"), MASTER_TRIM_DB)

	# The real kart bus, re-routed to send into the probe bus instead of into
	# Master. **An `AudioEffectCapture` sits upstream of its own bus's volume** --
	# measured here, by setting this bus's level and watching nothing move -- so a
	# capture on the same bus the master lives on cannot see the master at all, and
	# sections 4 and 5 would report the shipped chain minus the one number this
	# whole probe exists to set. Chaining the kart bus into the probe bus puts the
	# capture downstream of `MASTER_GAIN_DB` and measures the graph that ships.
	#
	# The same fact is why trimming Master keeps the room quiet without touching a
	# single figure in the report, which section 0 is the check on.
	var kart_bus := EngineVoiceRig.ensure_bus()
	var kart_index := AudioServer.get_bus_index(kart_bus)
	if kart_index > index:
		AudioServer.set_bus_send(kart_index, PROBE_BUS)


## The camera, which is the listener until section 2 says otherwise.
##
## Built and made current explicitly rather than relying on a scene, because the
## whole of question 2 is what Godot does when nobody has said where the listener
## is -- and a `--script` main loop with one camera in the tree is the same
## situation `proving_ground.tscn` is in with its chase rig.
func _build_listener() -> void:
	_camera = Camera3D.new()
	_camera.name = "ProbeCamera"
	get_root().add_child(_camera)
	_camera.current = true

	# Present but not current. Section 2's second half enables it, and building it
	# up front means the two halves differ by exactly one property.
	_listener = AudioListener3D.new()
	_listener.name = "ProbeListener"
	get_root().add_child(_listener)


## The two reference emitters: the same full-scale sine, one positional and one
## not, so that the 3 dB the pan costs is measured rather than reasoned about.
func _build_reference() -> void:
	_probe = ClassDB.instantiate("AudioProbe")
	_probe.set_mix_rate(_mix_rate)
	_probe.set_engine_synth(false)
	_probe.set_scrub_wind(false, false)
	_probe.set_physics_busy_us(0)
	_probe.set_use_table(false)
	_probe.set_partials(1)
	_probe.set_gain(REFERENCE_GAIN)
	_probe.set_fundamental_hz(REFERENCE_HZ)
	get_root().add_child(_probe)
	_probe.arm()

	_flat_player = AudioStreamPlayer.new()
	_flat_player.name = "FlatReference"
	_flat_player.stream = ClassDB.instantiate("AudioProbeStream")
	_flat_player.bus = PROBE_BUS
	get_root().add_child(_flat_player)

	_spatial_player = AudioStreamPlayer3D.new()
	_spatial_player.name = "SpatialReference"
	_spatial_player.stream = ClassDB.instantiate("AudioProbeStream")
	_spatial_player.bus = PROBE_BUS
	# The rig's settings, copied deliberately: this probe measures that
	# configuration and not a clean-room one.
	_spatial_player.volume_db = EngineVoiceRig.VOLUME_DB
	_spatial_player.unit_size = EngineVoiceRig.UNIT_SIZE
	_spatial_player.max_distance = EngineVoiceRig.MAX_DISTANCE
	_spatial_player.position = Vector3.ZERO
	get_root().add_child(_spatial_player)

	# A third player, configured as the rig configures the **scrub** emitter rather
	# than the engine one. `SCRUB_UNIT_SIZE` is 2.0 against the engine's 3.0 on
	# purpose -- `engine_voice_rig.gd` argues that a slide that carried as far as
	# the engine would make somebody else's slide sound like yours -- so measuring
	# the scrub through the engine's player would report a level the scrub never
	# has.
	_scrub_reference = AudioStreamPlayer3D.new()
	_scrub_reference.name = "ScrubReference"
	_scrub_reference.stream = ClassDB.instantiate("AudioProbeStream")
	_scrub_reference.bus = PROBE_BUS
	_scrub_reference.volume_db = EngineVoiceRig.VOLUME_DB
	_scrub_reference.unit_size = EngineVoiceRig.SCRUB_UNIT_SIZE
	_scrub_reference.max_distance = EngineVoiceRig.SCRUB_MAX_DISTANCE
	_scrub_reference.position = Vector3.ZERO
	get_root().add_child(_scrub_reference)


## The shipped rig on a real `KartBody`, built the way `voice_probe.gd` builds it.
##
## A bare body with no mesh and no collision shape. It complains and falls, which
## is fine and is why section 4 makes the camera follow it: nothing here reads the
## solver's motion, only the level of what the boundary publishes.
func _build_rig() -> void:
	_kart = KartBody.new()
	_kart.name = "Kart"
	get_root().add_child(_kart)
	_kart.input_driver = func() -> Dictionary:
		return {"throttle": RIG_THROTTLE, "brake": 0.0, "steer": 0.0}
	_kart.engage(1, 5.0)

	_engine_stream = EngineVoiceRig.attach(_kart)
	var noise: Array = EngineVoiceRig.attach_noise(_kart)
	if _engine_stream == null or noise.size() < 2 or noise[0] == null:
		printerr("error: EngineVoiceRig returned null with the classes registered.")
		quit(1)
		return

	# Found by the names the rig gives them, and **left on whatever bus the rig put
	# them on**. That is the point: the kart bus is chained into the probe bus in
	# `_build_bus`, so these are measured exactly as they ship, master included.
	# Moving them onto the probe bus directly is what this file did first, and it
	# reported the chain with its master silently missing.
	_engine_player = _kart.get_node("EngineVoice") as AudioStreamPlayer3D
	_scrub_player = _kart.get_node("ScrubVoice") as AudioStreamPlayer3D
	_wind_player = _kart.get_node("WindVoice") as AudioStreamPlayer


## The cells, in order. Each is `[section, label, applier]`.
func _build_cells() -> void:
	_cells.append(["0 calibration", "flat, unity",
		func() -> void: _reference(true, 0.0, REFERENCE_HZ, -1.0)])
	_cells.append(["0 calibration", "3D at %.2f m" % EngineVoiceRig.UNIT_SIZE,
		func() -> void: _reference(false, EngineVoiceRig.UNIT_SIZE, REFERENCE_HZ, 20500.0)])

	for distance in DISTANCES:
		_cells.append(["1 distance law", "%.2f m" % distance,
			func() -> void: _reference(false, distance, REFERENCE_HZ, 20500.0)])

	for distance in LISTENER_DISTANCES:
		_cells.append(["2 listener is the camera", "%.2f m" % distance,
			func() -> void: _reference(false, distance, REFERENCE_HZ, 20500.0)])
	for distance in LISTENER_DISTANCES:
		_cells.append(["2 AudioListener3D at the emitter", "%.2f m" % distance,
			func() -> void:
				_reference(false, distance, REFERENCE_HZ, 20500.0)
				_listener.position = Vector3.ZERO
				_listener.make_current()])

	for distance in FILTER_DISTANCES:
		for hz in FILTER_HZ:
			_cells.append(["3 filter at %.2f m, default cutoff" % distance, "%.0f Hz" % hz,
				func() -> void: _reference(false, distance, hz, -1.0)])
		for hz in FILTER_HZ:
			_cells.append(["3 filter at %.2f m, wide open" % distance, "%.0f Hz" % hz,
				func() -> void: _reference(false, distance, hz, 20500.0)])

	if _skip_rig:
		return

	_cells.append(["4 the engine note, shipped rig", "cockpit 0.52 m",
		func() -> void: _rig(0.52)])
	_cells.append(["4 the engine note, shipped rig", "chase 3.56 m",
		func() -> void: _rig(3.56)])

	# The scrub and wind layers are **not** measured through the `KartBody`, and
	# this is the second thing this probe got wrong before it got it right. Those
	# two are driven from `VehicleTelemetry`, and a body with no collision shape has
	# no contact, no slip and no road speed -- so the scrub cell measured silence
	# and the wind cell measured the kart's free-fall speed, which is not an
	# operating point any kart is ever in. Both printed cleanly.
	#
	# So they run through `AudioProbe`'s scrub-and-wind mode at a held operating
	# point, which is the same production call sequence `scrub_cost_probe.gd` uses,
	# on players configured exactly as `EngineVoiceRig` configures the real ones.
	# Wind alone comes from setting the scrub drive to zero rather than from a
	# difference of two cells: at zero drive the scrub layer's samples are zero,
	# which ADR-0038's cost table already relies on.
	_cells.append(["5 the noise layers, at a held operating point",
		"scrub, chase 3.56 m",
		func() -> void: _layers(SCRUB_DRIVE, false, 3.56)])
	# And at the distance the listener is **actually** at, which is not the same
	# question and is the one the mix is judged on. The scrub emitter sits at the
	# rear axle with `SCRUB_UNIT_SIZE` of 2.0, and the driver's head is well inside
	# that, so the clamp applies and the layer is several dB louder here than the
	# chase row suggests. Quoting the chase row as "the scrub level" would have
	# understated the thing being balanced.
	var head_to_axle := 3.56
	if _kart != null:
		head_to_axle = maxf(
			_kart.driver_head_position().distance_to(_kart.rear_axle_position()), 0.05)
	_cells.append(["5 the noise layers, at a held operating point",
		"scrub, driver's head %.2f m" % head_to_axle,
		func() -> void: _layers(SCRUB_DRIVE, false, head_to_axle)])
	_cells.append(["5 the noise layers, at a held operating point",
		"wind, non-positional",
		func() -> void: _layers(0.0, true, 3.56)])
	# There is deliberately no "both layers" cell. `AudioProbeStream` sums the two
	# into one stream, and in production they are two players on two different
	# paths -- one positional and one not. A summed cell would have to be routed
	# through one of them, and whichever was chosen would be wrong about the other.
	# The two rows above are each exact, and adding them is arithmetic.


## Configure the reference emitters for one cell.
##
## `flat` picks the non-positional player. `cutoff_hz` below zero means "leave
## Godot's default alone", which is the configuration `engine_voice_rig.gd` ships
## and therefore the one being measured.
func _reference(flat: bool, distance: float, hz: float, cutoff_hz: float) -> void:
	# Sections 0-3 measure the transfer function, so the reference players sit
	# directly on the probe bus and bypass the kart bus's master entirely -- which
	# is what lets the calibration row read a literal zero. Sections 4 and 5 go
	# through the kart bus and carry it.
	_flat_player.bus = PROBE_BUS
	_spatial_player.bus = PROBE_BUS
	_scrub_reference.bus = PROBE_BUS
	_follow_kart = false
	_silence_rig()
	_listener.clear_current()
	_camera.current = true
	_camera.position = Vector3(0.0, 0.0, maxf(distance, 0.01))
	_camera.look_at_from_position(_camera.position, Vector3.ZERO, Vector3.UP)

	_probe.set_partials(1)
	_probe.set_engine_synth(false)
	_probe.set_scrub_wind(false, false)
	_probe.set_gain(REFERENCE_GAIN)
	_probe.set_fundamental_hz(hz)

	_spatial_player.attenuation_filter_cutoff_hz = (
		_default_cutoff_hz if cutoff_hz < 0.0 else cutoff_hz)

	_play(_scrub_reference, false)
	_play(_flat_player, flat)
	_play(_spatial_player, not flat)


## Section 4: the real engine note, on the real `KartBody`, at the shipped gain.
func _rig(distance: float) -> void:
	_listener.clear_current()
	_camera.current = true
	_play(_flat_player, false)
	_play(_spatial_player, false)
	_play(_scrub_reference, false)

	# The camera rides with the kart at a fixed offset, because the kart is falling
	# and a fixed camera would make the distance a function of how long the cell
	# took. Straight behind, which is where the chase rig is.
	_follow_kart = true
	_follow_offset = Vector3(0.0, 0.0, distance)

	_play(_engine_player, true)
	_play(_scrub_player, false)
	_play(_wind_player, false)


## Section 5: one noise layer at a held operating point, on a player configured
## the way `EngineVoiceRig` configures the real one.
##
## Zero scrub drive plus wind is how the wind layer is isolated -- see the cell
## list. The positional player carries the scrub because that is what the rig
## does; the wind goes through the non-positional one for the same reason.
func _layers(drive: float, wind: bool, distance: float) -> void:
	# Through the kart bus, because a level figure for a layer that skipped the
	# master is a figure nobody can compare against section 4.
	_flat_player.bus = EngineVoiceRig.KART_BUS
	_scrub_reference.bus = EngineVoiceRig.KART_BUS
	_follow_kart = false
	_silence_rig()
	_listener.clear_current()
	_camera.current = true
	_camera.position = Vector3(0.0, 0.0, maxf(distance, 0.01))
	_camera.look_at_from_position(_camera.position, Vector3.ZERO, Vector3.UP)

	_probe.set_engine_synth(false)
	_probe.set_partials(0)
	_probe.set_scrub_wind_operating_point(drive, SCRUB_SPEED_MS, SCRUB_SURFACE)
	_probe.set_scrub_wind(true, wind)

	_play(_spatial_player, false)
	# Wind is the non-positional layer and scrub is the positional one, which is
	# ADR-0038 section 4's decision and is half of what makes their levels differ.
	_play(_flat_player, wind)
	_play(_scrub_reference, not wind)


func _silence_rig() -> void:
	for player in [_engine_player, _scrub_player, _wind_player]:
		_play(player, false)


func _play(player: Node, want: bool) -> void:
	if player == null:
		return
	if want and not player.playing:
		player.play()
	elif not want and player.playing:
		player.stop()


func _physics_process(_delta: float) -> bool:
	if not _started:
		_started = true
		# Read the shipped defaults off the player rather than asserting them, and
		# report them. A default that is not what this file predicts is a finding
		# about Godot, not a reason to stop.
		_default_cutoff_hz = _spatial_player.attenuation_filter_cutoff_hz
		_default_filter_db = _spatial_player.attenuation_filter_db
		_apply_cell()
		return false

	if _follow_kart and _kart != null:
		_camera.position = _kart.position + _follow_offset
		_camera.look_at_from_position(_camera.position, _kart.position, Vector3.UP)

	_drain()
	_phase_ticks += 1

	if _phase == "settle":
		if _phase_ticks >= int(round(_settle * _tick_hz)):
			_arm()
		return false

	if _phase_ticks < int(round(_seconds * _tick_hz)):
		return false

	_record()
	_cell += 1
	if _cell >= _cells.size():
		_report()
		quit(0)
		return true
	_apply_cell()
	return false


func _apply_cell() -> void:
	var applier: Callable = _cells[_cell][2]
	applier.call()
	_phase = "settle"
	_phase_ticks = 0


func _arm() -> void:
	_drain()
	_sum_left = 0.0
	_sum_right = 0.0
	_frames = 0
	_peak = 0.0
	_phase = "measure"
	_phase_ticks = 0


## Pull everything the mixer has produced since the last tick out of the ring.
##
## Always called, in both phases, so that the settle phase's frames are discarded
## by being consumed rather than by being left to overflow into the measured
## window. A ring that overflowed would silently splice two operating points
## together and the report would read as a smooth curve.
func _drain() -> void:
	var available := _capture.get_frames_available()
	if available <= 0:
		return
	var buffer := _capture.get_buffer(available)
	if _phase != "measure":
		return
	for frame in buffer:
		_peak = maxf(_peak, maxf(absf(frame.x), absf(frame.y)))
		_sum_left += frame.x * frame.x
		_sum_right += frame.y * frame.y
		_frames += 1


func _record() -> void:
	var count := float(maxi(_frames, 1))
	var mean_left := _sum_left / count
	var mean_right := _sum_right / count
	# Normalized so that a full-scale sine present in both channels reads 0.00 dB:
	# each channel's mean square is then 0.5 and the sum is 1.0.
	var total := mean_left + mean_right
	_rows.append([
		_cells[_cell][0], _cells[_cell][1],
		_db(sqrt(mean_left)), _db(sqrt(mean_right)),
		10.0 * (log(maxf(total, 1.0e-18)) / log(10.0)),
		_db(_peak), _frames,
	])
	# The rpm is read off the solver and the partial count off the audio thread's own
	# read-back, so a report that claimed an operating point the synth never saw
	# would disagree with itself. `voice_stats` carries no rpm -- `KartBody` does.
	if _cells[_cell][0].begins_with("4 ") and _engine_stream != null and _kart != null:
		var stats: Dictionary = _engine_stream.call("voice_stats")
		_rig_note = "%.0f rpm, %d partials, voice_gain %.2f" % [
			_kart.get_engine_rpm(), int(stats.get("partials", 0)),
			EngineVoiceRig.VOICE_GAIN]


## dBFS, with a floor so that a silent cell prints a number instead of `-inf`.
func _db(linear: float) -> float:
	return 20.0 * (log(maxf(linear, 1.0e-9)) / log(10.0))


## The prediction. Godot's `ATTENUATION_INVERSE_DISTANCE` is documented as a gain
## of `unit_size / distance`, expressed in dB, offset by `volume_db` and clamped
## above at `max_db`. Every term is named so that a disagreement points at a term.
##
## The 3.01 dB the pan costs per channel is **not** folded in here. The `total`
## column is the one this predicts, and the whole point of carrying both is that
## the pan is visible as the difference between `total` and the two channels.
func _predicted_db(distance: float) -> float:
	var linear := EngineVoiceRig.UNIT_SIZE / maxf(distance, 0.0001)
	var db := _db(linear) + EngineVoiceRig.VOLUME_DB
	var ceiling := 3.0
	if _spatial_player != null and "max_db" in _spatial_player:
		ceiling = _spatial_player.max_db
	return minf(db, ceiling)


func _report() -> void:
	var lines: Array[String] = []
	lines.append("")
	lines.append("=== what Godot's 3D audio path costs in level, and how loud the rig is ===")
	lines.append("")
	lines.append("    audio driver                 %s" % AudioServer.get_driver_name())
	lines.append("    mix rate                     %.1f Hz" % _mix_rate)
	lines.append("    physics tick rate            %.1f Hz" % _tick_hz)
	lines.append("    measured per cell            %.2f s after %.2f s settle"
		% [_seconds, _settle])
	lines.append("")
	lines.append("    player unit_size             %.3f m" % _spatial_player.unit_size)
	lines.append("    player max_distance          %.1f m" % _spatial_player.max_distance)
	lines.append("    player volume_db             %.2f dB" % _spatial_player.volume_db)
	lines.append("    attenuation_filter_cutoff    %.1f Hz   (Godot's default)"
		% _default_cutoff_hz)
	lines.append("    attenuation_filter_db        %.1f dB   (Godot's default)"
		% _default_filter_db)
	if "max_db" in _spatial_player:
		lines.append("    player max_db                %.2f dB" % _spatial_player.max_db)
	else:
		lines.append("    player max_db                absent on this build")
	if _rig_note != "":
		lines.append("    section 4 operating point    %s" % _rig_note)
	lines.append("    sections 4-5 bus master      %.2f dB" % EngineVoiceRig.MASTER_GAIN_DB)
	# Which gain each section is actually measuring, because the two halves of the
	# rig get their gains from two different places and a reader who assumed one
	# would misread the other by whatever they differ by. Section 4 is the real
	# `EngineVoiceStream` at the rig's own constant; section 5 is `AudioProbe`'s
	# scrub-and-wind mode, which configures from `ScrubWindConfig`'s **compiled**
	# defaults in `src/core/scrub_wind.h` and never sees the GDScript ones.
	lines.append("    section 4 gain source        engine_voice_rig.gd VOICE_GAIN %.3f"
		% EngineVoiceRig.VOICE_GAIN)
	lines.append("    section 5 gain source        src/core/scrub_wind.h compiled defaults")
	lines.append("")
	# The positional path's own offset, measured rather than reasoned about. Section
	# 0's second cell is an `AudioStreamPlayer3D` at exactly `unit_size`, where the
	# distance law predicts unity, so whatever it reads below the flat player is
	# what the positional path costs before any distance is involved at all.
	# Section 1's predictions carry it, and it is printed rather than folded in
	# silently -- it is a finding in its own right.
	var offset := 0.0
	for row in _rows:
		if row[0].begins_with("0 ") and row[1].begins_with("3D"):
			offset = row[4] - _predicted_db(EngineVoiceRig.UNIT_SIZE)
	lines.append("    dBFS. `total` is both channels summed in power and reads 0.00 for a")
	lines.append("    full-scale sine in both. `predicted` is the distance law plus the")
	lines.append("    positional path's own measured offset of %.2f dB, which is what an" % offset)
	lines.append("    AudioStreamPlayer3D costs against a plain AudioStreamPlayer at the")
	lines.append("    same gain and at unit distance, before any distance is involved.")

	var section := ""
	for row in _rows:
		if row[0] != section:
			section = row[0]
			lines.append("")
			lines.append("--- %s" % section)
			lines.append("")
			lines.append("    cell                     left    right    total     peak"
				+ "   predicted    delta   frames")
		var predicted := "        -"
		var delta := "       -"
		if section.begins_with("1 "):
			var p := _predicted_db(float(row[1].split(" ")[0])) + offset
			predicted = "%9.2f" % p
			delta = "%8.2f" % (row[4] - p)
		lines.append("    %-22s %7.2f  %7.2f  %7.2f  %7.2f   %s %s   %6d"
			% [row[1], row[2], row[3], row[4], row[5], predicted, delta, row[6]])

	lines.append("")
	for line in lines:
		print(line)
