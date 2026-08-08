class_name EngineVoiceRig
extends RefCounted

## The kart's voice: the engine note, the tire scrub and the wind. Issues #81,
## #82, #84.
##
## `src/audio/engine_voice.h` is the synthesizer's join to Godot's mixer and
## `src/core/engine_synth.h` is the synthesizer. This file is the *scene's* half of
## it: an `AudioStreamPlayer3D` at the engine mount, an `EngineVoiceStream` in it,
## and the `KartBody` told where to publish.
##
## ## Why this is a shared file when `_build_kart` is deliberately duplicated
##
## `test_track.gd` says its kart plumbing is copied from `proving_ground.gd` "almost
## verbatim, which is deliberate" — two scenes that build the same kart two ways
## would be two karts, and the duplication is cheap to read.
##
## That argument does not extend to the numbers below. Attenuation, unit size and
## gain are **tuning**, they are judged by ear rather than by a gate, and one of the
## two scenes is where every §6.4 figure is measured. Two copies that drifted would
## mean the kart sounded different on the track from how it sounded on the pad it
## was validated on, and nothing would report it. So the rig has one owner and the
## asset plumbing keeps its two.
##
## ## What is measured here and what is not
##
## Nothing. Every number in this file is a tunable and says so, which is the
## distinction `kz_audio_reference.h` spends its header on. The spectral constants
## are sourced and live there; how loud the thing is and how fast it falls off with
## distance are mixing decisions with no recording behind them. **Issue #160** owns
## the mixing pass, and this is the placeholder it will replace — #83 is shift and
## clutch sounds and never owned it, which #160 exists to correct.
##
## ## Three emitters, and the wind one is not in the world
##
## The engine note is an `AudioStreamPlayer3D` at the engine mount. The scrub is an
## `AudioStreamPlayer3D` at the kart, because M8's acceptance criterion is that an
## opponent is locatable by ear and a tire is where the kart is. **The wind is a
## plain `AudioStreamPlayer`** — it is at the driver's head, it is not a source in
## the world, and attenuating or Doppler-shifting it would be nonsense. That is
## also why there is one of it and never twelve.
##
## The one number here that is *not* arbitrary is the position, and it is not a
## number in this file at all — `KartBody.engine_mount_position()` serves it from
## `chassis.h`'s lump table, so the emitter cannot drift away from the 20 kg the
## solver is carrying.

## The bus the whole kart is mixed on, and the master level on it. Issue #160.
##
## **This is the only master level, and that is the point.** #160 records the
## symptom as "I have to turn the system volume up so high to hear the engine" and
## then records the cause of *that*: the three gains below were spread across the
## stream, the player and the synth, each chosen in isolation, with nothing owning
## the sum. Adjusting the ratio between two layers that are both under-level cannot
## fix a level, and `e7b0eaa` tried.
##
## Measured through a real CoreAudio mix by `tools/verify/audio_level_probe.gd`,
## which is the file that turned this from an argument into a number:
##
##     engine note at the chase camera        -33.08 dBFS
##     engine note at the driver's head       -28.6  dBFS
##     a primary voice wants about            -19    dBFS
##
## **The synth cannot supply the difference.** At `gain = 1.0` it peaks at
## -0.70 dBFS and is already inside its own soft-clip knee, so `VOICE_GAIN` has
## 9.03 dB in it and no more -- and the 15-20 dB of crest factor under that is a
## phase-aligned harmonic stack, not a conservative constant, so no multiplication
## recovers it. A bus can carry the level without fighting a clip knee that exists
## for a good reason.
##
## Both figures are `master_gain_db` in `src/core/tuning.h`, which is the row F2
## moves and `tuning.sh` audits. The constants here are its defaults, the way
## `VOICE_GAIN` is `voice_gain`'s.
const KART_BUS := "Kart"
const MASTER_GAIN_DB := 9.5

## Overall synth gain, linear, before the player's own volume and attenuation.
##
## **A tunable.** `EngineAudioConfig::gain` defaults to 0.35 and that default was
## chosen for headroom in an offline render rather than for a kart in a scene. Low
## enough here that the first drive cannot be painful.
##
## Left at 0.30 by #160's level pass, deliberately. What was wrong was the master,
## and raising this instead would have spent the synth's remaining 9 dB of peak
## margin on a problem that is 15-20 dB wide.
const VOICE_GAIN := 0.30

## Distance in meters at which the note is at full volume, and the model it falls
## off by beyond that.
##
## **A tunable, and the driver's own engine is the case the model fits worst.** An
## inverse-distance law is right for a kart two corners away and wrong for one
## bolted 600 mm behind your right ear, where the near field is all there is. A
## unit size roughly the size of the kart keeps the player inside the flat part of
## the curve, so the note does not swing in level as the chase camera moves.
##
## This is the part of the channel ADR-0035 deliberately did not measure — "Godot's
## own mixing, bussing and 3D attenuation" is in its "what this does not settle"
## list. It is guessed, it sounds acceptable, and it should be measured before
## anyone calls it done.
const UNIT_SIZE := 3.0
const MAX_DISTANCE := 150.0

## Decibels the emitter is trimmed by. Zero: the gain above is the volume knob, and
## two volume controls in series is how a channel ends up mixed by whichever one
## somebody found first.
const VOLUME_DB := 0.0

## The two noise layers' gains, linear. **Balance, not level** -- `MASTER_GAIN_DB`
## owns the level now, which is the split #160 asked for.
##
## `src/core/tuning.h` carries the same two values with the same reasoning and F2
## moves them from the first drive; this file is where they are applied rather than
## a second owner of them.
##
## Both moved on measurement rather than by ear, and the two measurements were
## taken by paths that never met -- pure C++ with no engine, and a real CoreAudio
## mix:
##
##   * **scrub 0.45 -> 0.035.** Full-slip scrub sat 16.7 dB (core) and 16.3 dB
##     (in-engine) *above* the engine note, and peaked at 1.052, past full scale.
##     22 dB down puts a full-lock slide about 6 dB under the engine. The 6 dB is a
##     preference and is owed an ear; the 22 is not.
##   * **wind 0.12 -> 0.030**, and this one is not a judgement changing. The plan
##     was to revert `e7b0eaa`'s 0.20 -> 0.12 once the master was right, because
##     that change was made on a drive where the engine was 15-20 dB under level
##     and so measured too much wind where there was too little of everything
##     else. Then the wind layer was normalized by its own filter's RMS gain --
##     the defect `193d507` had already fixed for scrub and missed here -- and
##     that made it **13 to 20 dB louder at the same gain**, most at low speed,
##     because it is the 2.5 dB per doubling the un-normalized filter was adding.
##     0.12 after the fix is louder than the engine. 0.030 puts it about 10 dB
##     under at 30 m/s, which is roughly where 0.12 sat before.
const SCRUB_GAIN := 0.035
const WIND_GAIN := 0.030

## The shift/clutch and rolling layers' gains, linear. Issues #83 and #85.
##
## **Balance, not level**, on the same terms as the two above: `MASTER_GAIN_DB`
## owns the level. `src/core/shift_audio.h` and `src/core/roll_audio.h` carry the
## same values as their config defaults with the reasoning attached, and this file
## applies them rather than being a second owner.
##
## All three are **estimated and owed an ear**, and all three were set against a
## stated audibility criterion rather than by taste. The full sweeps and the
## reasoning live with the constants in `src/core/shift_audio.h` and
## `src/core/roll_audio.h`; the short version:
##
##   * **clack 0.20.** At the 0.055 this started at, the engagement transient sat
##     +9.6 dB over its own background against +10.1 dB for the engine's torque cut
##     alone — the layer was making the shift very slightly *less* prominent than
##     no layer at all. At 0.20 it is +15.4 dB, with a stem peak of -10.6 dBFS,
##     level with the engine note for the 9 ms the impact lasts.
##   * **clutch 0.09.** At 0.030 the stem peaked at -24.1 dBFS, 14 dB under the
##     note on a launch — present in a meter and not in a room. 0.09 puts it about
##     4 dB under.
##   * **rolling 0.09.** Chosen as the smallest value that makes the four surfaces
##     distinguishable: level spread across asphalt / curb / grass / dirt at a
##     constant 20 m/s goes from **0.03 dB before this change to 3.94 dB**, and the
##     spectral centroid spread from 81 Hz to 2168 Hz. It is also the layer that
##     costs the most headroom, and it is a game-feel decision rather than a
##     physical one — a real driver cannot hear their tires over a KZ at all.
##
## Both of the first two started an order lower and were **measured to be
## inaudible**, which is the failure that matters more than being too loud: a layer
## nobody can hear is a layer that did not ship. #160's mixing pass owns the final
## balance.
const CLACK_GAIN := 0.20
const CLUTCH_GAIN := 0.09
const ROLL_GAIN := 0.09

## Where the rolling emitter sits, and how it falls off.
##
## At the rear axle with the scrub, because rolling noise comes off the contact
## patches and the scrub layer already made that call for the same reason — one
## layer for four corners, so putting it at one wheel would place it wrong three
## quarters of the time.
##
## **A shorter reach than the scrub's**, which is the one number here with an
## argument rather than a preference behind it. M8's acceptance criterion is that an
## opponent is locatable by ear, and what locates them is the engine note. Rolling
## noise from a kart two corners away would be a wash of hiss with no information
## in it, competing with the cue that does carry information. 30 m is about the
## distance at which a kart is still a distinct thing rather than part of the field.
const ROLL_UNIT_SIZE := 2.0
const ROLL_MAX_DISTANCE := 30.0

## The gearbox emitter, at the engine mount.
##
## The gearbox on a KZ is bolted to the engine and shares its cases, so the clack
## comes from where the note does — `KartBody.engine_mount_position()` serves both,
## and the shift layer reusing it means the two cannot drift apart. A shift heard
## from a different place than the engine it interrupts is the kind of thing nobody
## can name and everybody notices.
const SHIFT_UNIT_SIZE := 3.0
const SHIFT_MAX_DISTANCE := 90.0

## Where the scrub emitter sits, and how it falls off.
##
## At the middle of the rear axle rather than at any one contact patch: there is one
## scrub layer for four corners — `kart_body.cpp` takes the mean of the four slip
## angles and says why — so placing it at one wheel would put the sound of all four
## in the wrong place three quarters of the time.
##
## A smaller unit size than the engine's, deliberately. The engine has to stay
## audible across a field; scrub is the cue that tells you *your* tires are going,
## and one that carried as far as the engine would make somebody else's slide sound
## like yours.
const SCRUB_UNIT_SIZE := 2.0
const SCRUB_MAX_DISTANCE := 60.0

## Make sure the kart bus exists, and return its name.
##
## Created in code rather than in a `.tres` bus layout, because a bus layout is a
## binary resource that no reviewer can read in a diff and this project's whole
## audit story is that a number is readable next to the argument for it. It is also
## idempotent, which matters: two scenes and four probes call this, and
## `AudioServer`'s bus list survives a scene change.
##
## Falls back to Master rather than erroring if the bus cannot be made. A kart that
## is 9.5 dB quiet is a bad mix; a kart that is silent because a bus was missing is
## a bug report about the synth.
static func ensure_bus() -> String:
	var index := AudioServer.get_bus_index(KART_BUS)
	if index < 0:
		index = AudioServer.bus_count
		AudioServer.add_bus(index)
		AudioServer.set_bus_name(index, KART_BUS)
		AudioServer.set_bus_send(index, "Master")
	if AudioServer.get_bus_index(KART_BUS) < 0:
		push_warning("EngineVoiceRig: could not create the '%s' bus; using Master."
			% KART_BUS)
		return "Master"
	AudioServer.set_bus_volume_db(index, MASTER_GAIN_DB)
	return KART_BUS


## Put the listener at the driver's head and make it current. Issue #160.
##
## **Without this the listener is whichever `Camera3D` is current**, which was
## measured rather than assumed: `tools/verify/audio_level_probe.gd` moves the
## camera with the emitter fixed and the level swings 20.7 dB between 1 m and 20 m,
## then makes an `AudioListener3D` current and the level is flat at every distance.
##
## Three things follow from that, and only the first is about loudness:
##
##   1. The chase camera sits 3.56 m back, which costs 1.53 dB against unit
##      distance. The driver's head is 0.52 m from the engine mount, inside the
##      `max_db` clamp Godot applies at +3.0 dB, so moving the listener there is
##      worth 4.5 dB and is the only part of #160's deficit available for free.
##   2. **The cockpit view and the chase view otherwise hear two different mixes**,
##      and every judgement made so far was made on one of them. A mix that changes
##      when you press V is not a mix that can be tuned.
##   3. The wind layer is a plain `AudioStreamPlayer` at the driver's head already
##      (ADR-0038 section 4). With the listener on a camera three meters behind the
##      kart, the wind was at the driver's head and the ears were not.
##
## The position is served by `KartBody` from `chassis.h`'s lump table rather than
## typed here, so it moves when issue #107's seat calibration does.
static func attach_listener(kart: KartBody) -> AudioListener3D:
	if kart == null:
		return null
	var listener := AudioListener3D.new()
	listener.name = "DriverEars"
	# The cockpit camera's eye, not `driver_head_position()`. That method returns
	# `chassis.h`'s head **mass lump**, which is where the driver's mass is carried
	# for the yaw inertia and is not where he looks or hears from. Once #195 moved
	# the cockpit eye onto KART_SPEC 60.1.4's sourced hard point, the two were
	# **371 mm apart** -- and ADR-0039 measured 20.7 dB of level swing over listener
	# range, so a third of a meter is not free. The ears go where the eyes are.
	listener.position = CockpitCamera.EYE
	kart.add_child(listener)
	# Both, and this is the same class of trap as `autoplay` below. `make_current`
	# on a node that is not yet in the tree does nothing, and a node parented during
	# a `--script` main loop's `_initialize` is exactly that case -- CLAUDE.md's
	# trap list records it and `shoot.gd`, `drive_probe.gd` and this file's own
	# `attach` all work around it. The deferred call lands after the node has
	# entered the tree whichever way it got there, and calling it twice is
	# idempotent.
	if listener.is_inside_tree():
		listener.make_current()
	listener.call_deferred("make_current")
	return listener


## Build the voice, parent it to the kart, and point the kart at it.
##
## Returns the stream so a caller can read `voice_stats()` — the HUD wants the
## worst-block figure — or null when the extension is not registered, which is the
## case every headless gate runs in.
static func attach(kart: KartBody) -> Object:
	if kart == null:
		return null
	# Absent rather than broken: `tests/run.sh` has no engine at all and
	# `drive.sh` runs with the Dummy audio driver, where a cost or latency figure
	# is wrong in scale and in shape (ADR-0035). A gate that silently gets no
	# voice is correct; a gate that errors because it has no speakers is not.
	if not ClassDB.class_exists("EngineVoiceStream"):
		return null

	var stream: AudioStream = ClassDB.instantiate("EngineVoiceStream")
	if stream == null:
		return null
	stream.set("gain", VOICE_GAIN)

	var player := AudioStreamPlayer3D.new()
	player.name = "EngineVoice"
	player.stream = stream
	player.bus = ensure_bus()
	player.volume_db = VOLUME_DB
	player.unit_size = UNIT_SIZE
	player.max_distance = MAX_DISTANCE
	# Parented to the kart rather than to the scene, so the emitter moves with the
	# engine instead of being repositioned every tick from `_process`. The position
	# comes from `chassis.h`'s lump table by way of the boundary — see the header.
	player.position = kart.engine_mount_position()
	kart.add_child(player)

	# By path, because that is the property `KartBody` exposes and a scene built in
	# the editor would set the same one. Relative to the kart, which is where the
	# player was just parented.
	kart.engine_voice_player = NodePath("EngineVoice")

	# Playing from the moment it enters the tree, and never stopped. A synth's
	# silence is rpm zero, not a stopped stream: starting the playback is what runs
	# `EngineVoiceStream::_instantiate_playback`, which is where
	# `EngineSynth::configure` fills a 4,096-point sine table and both ladders, and
	# doing that at the first throttle input would be a table fill on the frame the
	# driver pulls away.
	#
	# **`autoplay` and not `play()`, and this is a bug that was caught rather than
	# avoided.** `play()` here fails with "Playback can only happen when a node is
	# inside the scene tree" whenever the kart is not yet in the tree — which is
	# exactly the case CLAUDE.md documents for a `--script` main loop, where
	# `get_root().add_child()` parents a node but `_ready` has not run. It happens to
	# work from a scene's `_ready` and to fail from a probe's `_initialize`, so the
	# call site decided whether the kart made any sound at all. `autoplay` is
	# resolved by Godot when the player enters the tree, whenever that is, which
	# takes the ordering out of every caller's hands.
	player.autoplay = true
	if player.is_inside_tree():
		player.play()
	return stream


## Build the two noise layers and point the kart at them. Issue #84.
##
## Returns `[scrub_stream, wind_stream]`, either of which may be null, so a caller
## can read `voice_stats()` for the HUD. Separate from `attach` rather than folded
## into it because a scene may legitimately want the engine and not these — the
## turntable and the look scenes do — and because the failure of one layer to
## register should not take the engine note with it.
static func attach_noise(kart: KartBody) -> Array:
	if kart == null:
		return [null, null]
	# Absent rather than broken, same as `attach`: `drive.sh` runs under the Dummy
	# driver and `tests/run.sh` has no engine at all.
	if not ClassDB.class_exists("NoiseVoiceStream"):
		return [null, null]

	# LAYER_SCRUB = 0, LAYER_WIND = 1. Named through the property rather than by
	# integer at each call site would be better, but `ClassDB.instantiate` hands back
	# an `Object` and the enum is only reachable through the class. The C++ side
	# clamps anything that is not 1 to scrub, so a wrong value here is a scrub layer
	# in the wrong place and not undefined behavior.
	var scrub_stream: AudioStream = ClassDB.instantiate("NoiseVoiceStream")
	var wind_stream: AudioStream = ClassDB.instantiate("NoiseVoiceStream")
	if scrub_stream == null or wind_stream == null:
		return [null, null]
	scrub_stream.set("layer", 0)
	scrub_stream.set("gain", SCRUB_GAIN)
	wind_stream.set("layer", 1)
	wind_stream.set("gain", WIND_GAIN)

	var bus := ensure_bus()

	var scrub_player := AudioStreamPlayer3D.new()
	scrub_player.name = "ScrubVoice"
	scrub_player.stream = scrub_stream
	scrub_player.bus = bus
	scrub_player.volume_db = VOLUME_DB
	scrub_player.unit_size = SCRUB_UNIT_SIZE
	scrub_player.max_distance = SCRUB_MAX_DISTANCE
	scrub_player.position = kart.rear_axle_position()
	kart.add_child(scrub_player)
	scrub_player.autoplay = true
	if scrub_player.is_inside_tree():
		scrub_player.play()

	# Not an `AudioStreamPlayer3D`, and not parented to the kart either. Wind is what
	# the driver hears, not what the world emits; a 3D player would attenuate it with
	# the chase camera's distance, which is exactly backwards.
	var wind_player := AudioStreamPlayer.new()
	wind_player.name = "WindVoice"
	wind_player.stream = wind_stream
	wind_player.bus = bus
	wind_player.volume_db = VOLUME_DB
	kart.add_child(wind_player)
	wind_player.autoplay = true
	if wind_player.is_inside_tree():
		wind_player.play()

	kart.scrub_voice_player = NodePath("ScrubVoice")
	kart.wind_voice_player = NodePath("WindVoice")

	# The shift/clutch layer, at the engine mount, and the rolling layer, at the rear
	# axle. Issues #83 and #85.
	#
	# **Built here rather than in a third function**, and `kart_rig.gd` needs no edit
	# for them: it reads `noise[0]` and `noise[1]` and a longer array is invisible to
	# it. That was the constraint — `kart_rig.gd`, `circuit.gd` and `test_track.gd`
	# are all owned elsewhere — and it is also the right shape, because these two are
	# noise layers on the same class as the two above and a scene that wants tire
	# scrub wants rolling noise.
	#
	# Failure of either is silence for that layer and nothing else. `LAYER_SHIFT = 2`
	# and `LAYER_ROLL = 3`; the C++ side clamps anything out of range to scrub, so a
	# wrong value here is a misplaced layer rather than undefined behavior.
	var shift_stream: AudioStream = ClassDB.instantiate("NoiseVoiceStream")
	var roll_stream: AudioStream = ClassDB.instantiate("NoiseVoiceStream")
	if shift_stream == null or roll_stream == null:
		return [scrub_stream, wind_stream, null, null]
	shift_stream.set("layer", 2)
	shift_stream.set("gain", CLACK_GAIN)
	shift_stream.set("clutch_gain", CLUTCH_GAIN)
	roll_stream.set("layer", 3)
	roll_stream.set("gain", ROLL_GAIN)

	var shift_player := AudioStreamPlayer3D.new()
	shift_player.name = "ShiftVoice"
	shift_player.stream = shift_stream
	shift_player.bus = bus
	shift_player.volume_db = VOLUME_DB
	shift_player.unit_size = SHIFT_UNIT_SIZE
	shift_player.max_distance = SHIFT_MAX_DISTANCE
	shift_player.position = kart.engine_mount_position()
	kart.add_child(shift_player)
	shift_player.autoplay = true
	if shift_player.is_inside_tree():
		shift_player.play()

	var roll_player := AudioStreamPlayer3D.new()
	roll_player.name = "RollVoice"
	roll_player.stream = roll_stream
	roll_player.bus = bus
	roll_player.volume_db = VOLUME_DB
	roll_player.unit_size = ROLL_UNIT_SIZE
	roll_player.max_distance = ROLL_MAX_DISTANCE
	roll_player.position = kart.rear_axle_position()
	kart.add_child(roll_player)
	roll_player.autoplay = true
	if roll_player.is_inside_tree():
		roll_player.play()

	# Assigned AFTER `add_child`, same as the two above: the setter resolves the path
	# relative to the kart and re-reads the stream, and `KartBody.set_shift_voice_player`
	# also pushes `Gearbox::shift_time` into the stream at that moment. Assigning
	# before the player is in the tree would leave the synth on its compiled default
	# duration — which is the same value today and would silently stop being so the
	# moment anybody tunes the gearbox.
	kart.shift_voice_player = NodePath("ShiftVoice")
	kart.roll_voice_player = NodePath("RollVoice")
	return [scrub_stream, wind_stream, shift_stream, roll_stream]


## Route the five audio tunables from a `KartTuning` into this voice.
##
## **This is the half of the registry that `KartTuning` deliberately cannot do.**
## It knows how to push a vehicle constant, because `KartBody` has a setter for
## each one; it does not know that `voice_unit_size` means an `AudioStreamPlayer3D`
## property or that `noise_gain` means an atomic on a stream, and a registry that
## learned both would be a registry that has to be edited every time the audio
## graph changes. So it emits `tuning_changed` and the audio side subscribes.
##
## Without this call the audit tells a lie of exactly the kind the whole system
## exists to prevent: `tuning.sh` reports `noise_gain` as moved while the synth
## the driver is listening to is unchanged. That is worse than having no knob,
## because the file would record a judgement made against a sound nobody heard.
##
## The three constants above stay as the defaults they always were —
## `src/core/tuning.h` carries the same three values and the same reasoning, and
## this file is where they are applied rather than a second owner of them.
static func bind_tuning(tuning: Node, stream: Object, player: AudioStreamPlayer3D,
		scrub_stream: Object = null, wind_stream: Object = null,
		shift_stream: Object = null, roll_stream: Object = null) -> void:
	if tuning == null or player == null:
		return

	# **The two new layers are found rather than passed, and that is deliberate.**
	# `circuit.gd:754` and `test_track.gd:643` are the only two callers and neither
	# is owned here, so the signature could not grow a required parameter — the two
	# optional ones above exist for a future caller that wants to be explicit, and
	# every current caller passes neither.
	#
	# Looking them up off the player's own parent is sound because that parent is the
	# `KartBody` and `attach_noise` above is what named the nodes. If a scene mounted
	# its own shift layer under a different name it simply is not tuned, which is the
	# same degradation the whole rig already has for a missing stream: silence or a
	# default, never an error.
	var kart := player.get_parent()
	if shift_stream == null and kart != null:
		var found := kart.get_node_or_null("ShiftVoice") as AudioStreamPlayer3D
		if found != null:
			shift_stream = found.stream
	if roll_stream == null and kart != null:
		var found_roll := kart.get_node_or_null("RollVoice") as AudioStreamPlayer3D
		if found_roll != null:
			roll_stream = found_roll.stream

	var apply := func(key: String, value: float, _owner: int) -> void:
		match key:
			# The master, and the one row here that is not a property of a node or a
			# stream. It is a bus volume, which is exactly why the registry cannot
			# push it itself -- see this function's own header.
			"master_gain_db":
				var index := AudioServer.get_bus_index(KART_BUS)
				if index >= 0:
					AudioServer.set_bus_volume_db(index, value)
			"voice_gain":
				if stream != null:
					stream.set("gain", value)
			"voice_unit_size":
				player.unit_size = value
			"voice_max_distance":
				player.max_distance = value
			"comb_depth":
				if stream != null:
					stream.set("comb_depth", value)
			"noise_gain":
				if stream != null:
					stream.set("noise_gain", value)
			# Issue #84's eight. The two gains land on the streams; the six spectral
			# rows land on the C++ config, which the playback re-reads every mix
			# block — see `NoiseVoiceStream::set_tuning_*`.
			"scrub_gain":
				if scrub_stream != null:
					scrub_stream.set("gain", value)
			"wind_gain":
				if wind_stream != null:
					wind_stream.set("gain", value)
			"scrub_center_hz", "scrub_q", "scrub_gamma", "scrub_full_speed":
				if scrub_stream != null:
					scrub_stream.set(key, value)
			"wind_cutoff_per_ms", "wind_speed_exponent":
				if wind_stream != null:
					wind_stream.set(key, value)
			# #83's and #85's. `clack_gain` and `roll_gain` land on each stream's
			# `gain`, because `gain` is what a `NoiseVoiceStream` calls its own layer
			# level whatever the layer is; everything else is named identically on
			# both sides so the key passes straight through.
			"clack_gain":
				if shift_stream != null:
					shift_stream.set("gain", value)
			"clutch_gain", "clack_center_hz", "clack_q", "clutch_center_hz", \
			"clutch_full_slip":
				if shift_stream != null:
					shift_stream.set(key, value)
			"roll_gain":
				if roll_stream != null:
					roll_stream.set("gain", value)
			"roll_cutoff_hz", "roll_db_per_doubling", "roll_rough_brightness":
				if roll_stream != null:
					roll_stream.set(key, value)
			# Every other key belongs to the vehicle or the controller and is
			# applied by the registry itself. Not an error, and not silent either:
			# falling through here is the normal case for the nine that are not
			# audio at all.
			_:
				pass
	tuning.tuning_changed.connect(apply)
	# And once now, because the registry's own first push may already have happened
	# — it is deferred out of `_ready` and this rig is attached from a scene's
	# `_ready` too, so which came first is not something either side should have to
	# know. Pushing again is idempotent.
	tuning.apply_all()
