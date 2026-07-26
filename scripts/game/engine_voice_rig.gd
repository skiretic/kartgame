class_name EngineVoiceRig
extends RefCounted

## The kart's engine note, mounted where the engine is. Issues #81, #82.
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
## distance are mixing decisions with no recording behind them. Issue #83 owns the
## mixing pass, and this is the placeholder it will replace.
##
## The one number here that is *not* arbitrary is the position, and it is not a
## number in this file at all — `KartBody.engine_mount_position()` serves it from
## `chassis.h`'s lump table, so the emitter cannot drift away from the 20 kg the
## solver is carrying.

## Overall synth gain, linear, before the player's own volume and attenuation.
##
## **A tunable.** `EngineAudioConfig::gain` defaults to 0.35 and that default was
## chosen for headroom in an offline render rather than for a kart in a scene. Low
## enough here that the first drive cannot be painful; #83 sets it properly against
## the other layers, which do not exist yet.
const VOICE_GAIN := 0.18

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
static func bind_tuning(tuning: Node, stream: Object, player: AudioStreamPlayer3D) -> void:
	if tuning == null or player == null:
		return
	var apply := func(key: String, value: float, _owner: int) -> void:
		match key:
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
			# Every other key belongs to the vehicle or the controller and is
			# applied by the registry itself. Not an error, and not silent either:
			# falling through here is the normal case for nine of the fourteen.
			_:
				pass
	tuning.tuning_changed.connect(apply)
	# And once now, because the registry's own first push may already have happened
	# — it is deferred out of `_ready` and this rig is attached from a scene's
	# `_ready` too, so which came first is not something either side should have to
	# know. Pushing again is idempotent.
	tuning.apply_all()
