extends SceneTree

## M6's acceptance harness: record a lap's input stream, save it, load it back,
## re-simulate from it, and compare the state hash tick for tick.
##
##     tools/verify/replay.sh
##     godot --headless --path . --script tools/verify/replay_probe.gd -- \
##         --mode=record --out=user://replay_probe/a.replay
##     godot --headless --path . --script tools/verify/replay_probe.gd -- \
##         --mode=play   --in=user://replay_probe/a.replay
##
## ROADMAP M6 states the criterion as "a recorded lap re-sims to an identical
## state hash", and its second half as "the harness fails loudly when determinism
## is deliberately broken in a test". Both halves are here, and the second one is
## `--break`, whose exit code is INVERTED: a sabotage that is *not* caught is the
## failure.
##
## ## What this is measuring, and what it is not
##
## `tools/verify/drive.sh` already runs the same scenario twice and compares two
## hashes. That proves the simulation is reproducible from the *same source of
## input*. This proves something strictly stronger and different: that the input
## stream survives a round trip through a file — quantized, encoded to nine bytes
## a tick, written, read back, decoded — and still drives the solver to the same
## state. Everything between `replay_snap` and `replay_decode_input` is what
## `drive.sh` cannot see.
##
## **The recording is the thing under test, not the scenario.** The scenario is an
## open-loop function of the tick counter for the reason `drive_probe.gd` gives at
## length: a closed loop measures the controller as much as the kart, and two runs
## of a controller that reads its own noisy state are not guaranteed to agree.
##
## ## The quantization rule, which is the whole of ADR-0041
##
## Input is snapped **upstream of the vehicle**, and the identical snapped
## Dictionary goes to `KartBody` and to the recorder. If it were snapped on the way
## into the file instead, the live run would have consumed full-precision values
## and the replay rounded ones, the two would diverge, and the divergence would
## look exactly like a solver bug. `KartReplay.record_input` refuses off-grid input
## rather than rounding it, so this probe cannot make that mistake quietly —
## `--break=grid` is the proof that the refusal is live.
##
## ## Why the hash is cumulative
##
## Each checkpoint absorbs the state and carries every state before it, so the
## first checkpoint that disagrees is the first tick at which the two runs are not
## the same simulation. At the default interval of 1 that is an exact tick number
## rather than a window, which is what "report the first divergent tick" has to
## mean to be actionable. 16 bytes a tick is 58 kB over this run — the format's own
## 120-tick default exists for a 15-minute race, not for a gate.

const DEFAULT_SCENE := "res://scenes/game/test_track.tscn"

## 3,000 ticks at 120 Hz is 25 s, which on the test track's 1,030 m loop is a lap's
## worth of driving with the corners in it. Long enough that a divergence has room
## to grow, short enough that the gate runs in under a minute.
const DEFAULT_TICKS := 3000

## One checkpoint per tick. See the header note.
const DEFAULT_INTERVAL := 1

## Where the probe writes. **A private directory, and this matters.** Every
## worktree shares one `user://` — it is keyed on `application/config/name`, so
## `~/Library/Application Support/Godot/app_userdata/kartgame/` is a single
## directory for the main checkout and every agent at once. Writing a replay under
## a real circuit's slug from inside a worktree would land on the real one.
const PRIVATE_DIR := "user://replay_probe"

## Ticks ignored before the scenario starts driving. The kart is spawned above the
## ground and dropped; the first fraction of a second is a landing. Recorded and
## re-simulated like any other tick — the drop is part of the state the replay has
## to reproduce, and it is the most sensitive part of the whole run.
const SETTLE_TICKS := 60

## A velocity nudge, in m/s, for `--break=state`. 1e-3 is ten times the state
## hash's own 1e-4 quantization grid, so it is guaranteed to be visible rather
## than rounded away — a negative control that could be swallowed by the
## comparison grid is not a negative control.
const STATE_NUDGE_MS := 0.001

var _kart: KartBody
var _root: Node
var _hash: KartStateHash
var _replay: KartReplay

var _mode := "record"
var _scene_path := DEFAULT_SCENE
var _scenario := "mixed"
var _ticks := DEFAULT_TICKS
var _interval := DEFAULT_INTERVAL
var _path := ""
var _break := ""
var _break_tick := 900
## How many quantization codes `--break=input` moves the throttle by. One is the
## smallest change the format can express — 1.53e-5 of throttle — and is the
## default because a negative control should be the hardest case the gate has to
## catch, not a comfortable one. A lever rather than a constant so that the
## detection floor can be *measured* by sweeping it instead of asserted.
var _break_codes := 1
var _quiet := false

var _tick := 0
var _input_calls := 0
var _failed := false
var _notes: Array[String] = []

## The hash after every checkpoint, in order. Printed so a human can diff two runs
## by eye and so `replay.sh` can compare a prefix without parsing a verdict.
var _sequence: PackedStringArray = []

var _distance := 0.0
var _top_speed := 0.0
var _previous_position := Vector3.ZERO
var _sabotage_applied := false
## Set once `_report` has run, so a Callable consulted after the end of the run
## returns neutral rather than appending a tick the header does not declare.
var _done := false


func _initialize() -> void:
	var args := Cmdline.parse()
	_mode = Cmdline.as_string(args, "mode", "record")
	_scene_path = Cmdline.as_string(args, "scene", DEFAULT_SCENE)
	_scenario = Cmdline.as_string(args, "scenario", "mixed")
	_ticks = Cmdline.as_int(args, "ticks", DEFAULT_TICKS)
	_interval = maxi(1, Cmdline.as_int(args, "interval", DEFAULT_INTERVAL))
	_break = Cmdline.as_string(args, "break", "")
	_break_tick = Cmdline.as_int(args, "break-tick", 900)
	_break_codes = maxi(1, Cmdline.as_int(args, "break-codes", 1))
	_quiet = Cmdline.as_bool(args, "quiet", false)

	if _mode == "scrub":
		scrub()
		quit(0)
		return
	_path = Cmdline.as_string(args, "out", "")
	if _path.is_empty():
		_path = Cmdline.as_string(args, "in", "")
	if _path.is_empty():
		_path = PRIVATE_DIR + "/probe.replay"

	# Registration is checked by name rather than assumed. **`ClassDB.class_exists`
	# is only true for engine and GDExtension types**, which is exactly what both of
	# these are — the trap it carries is for a GDScript `class_name`, where it is
	# always false and silently skips whatever it guards.
	for required in ["KartReplay", "KartStateHash", "KartSession", "KartBody"]:
		if not ClassDB.class_exists(required):
			printerr("%s is not registered -- build the extension:" % required)
			printerr("  PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \\")
			printerr("      scons target=editor arch=arm64")
			quit(1)
			return

	DirAccess.make_dir_recursive_absolute(_path.get_base_dir())

	_hash = KartStateHash.new()
	_replay = KartReplay.new()

	var packed: PackedScene = load(_scene_path)
	if packed == null:
		printerr("could not load " + _scene_path)
		quit(1)
		return
	_root = packed.instantiate()
	get_root().add_child(_root)

	if not _quiet:
		print("mode %s, scenario %s, %d ticks at %d Hz, interval %d" % [
			_mode, _scenario, _ticks, Engine.physics_ticks_per_second, _interval,
		])
		print("scene %s" % _scene_path)
		if not _break.is_empty():
			print("SABOTAGE %s at tick %d -- this run MUST be caught" % [_break, _break_tick])


## The kart and the session, found on the first physics tick.
##
## A `--script` main loop adds its scene before the tree starts running, so the
## node is parented but its `_ready` has not run and it has no children to find.
## `drive_probe.gd` and `shoot.gd` both have this shape for the same reason.
func _attach() -> bool:
	_kart = _root.find_child("Kart", true, false) as KartBody
	if _kart == null:
		printerr("no Kart in %s -- is assets/generated/kart.glb built?" % _scene_path)
		return false

	# **The same gate `drive_probe.gd` puts before its measurements, and it belongs
	# here for a sharper reason.** A replay's `config_hash` covers the tuning
	# preset, so a recording made under a preset refuses to play back against the
	# defaults — correctly, and 3,000 ticks after the cause. Refusing at tick 0 is
	# the same check paid for at a thousandth of the price.
	for node in _root.get_tree().get_nodes_in_group("tuning_source"):
		if not node.has_method("is_at_defaults") or node.is_at_defaults():
			continue
		printerr("the scene is tuned: %d changed, hash %s" % [
			node.changed_count(), node.tuning_hash_hex()])
		printerr("run tools/verify/tuning.sh to see what moved.")
		return false

	if not _begin():
		return false

	_kart.input_driver = _scripted_input
	_previous_position = _kart.global_position
	return true


## The configuration this run is recorded under, or played back against.
##
## Built here rather than taken from the scene, and that is deliberate: the scene's
## own `KartSession` is filled from *its* command line, so a probe that adopted it
## would record a configuration that changes when an unrelated flag does. This one
## is a pure function of the probe's own arguments, so a recording and a playback
## launched with the same arguments agree by construction, and one launched with
## different arguments refuses by construction — which is the behavior
## `--break=config` exercises.
func _session() -> KartSession:
	var session := KartSession.new()
	session.set_track("replay-probe")
	session.set_track_hash_hex("0x" + _scene_path.sha256_text().substr(0, 16))
	session.set_layout(KartSession.LAYOUT_FORWARD)
	session.set_condition(KartSession.CONDITION_DRY)
	session.set_type(KartSession.TYPE_PRACTICE)
	session.set_kart_class(KartSession.CLASS_KZ2)
	session.set_limit(KartSession.LIMIT_OPEN, 0.0)
	session.set_entry_count(1)
	session.set_roster_hash_hex("0x0000000000000001")
	session.set_seed_hex("0x00000000005eed01")
	# **Stamped from the engine rather than hardcoded.** Since issue #174 the tick
	# rate is inside the hash, so a 120 Hz recording re-simulated at 240 Hz refuses
	# naming `tick_hz` instead of diverging for a reason nobody could name.
	session.set_tick_hz(Engine.physics_ticks_per_second)
	return session


func _begin() -> bool:
	var session := _session()
	if not session.is_valid():
		printerr("the probe's own session is not valid, which is a bug in this file")
		return false

	if _mode == "record":
		if not _replay.set_hash_interval(_interval):
			printerr("interval %d refused" % _interval)
			return false
		if not _replay.begin_record(session):
			return false
		return true

	# Playback. Load first, then admit against the live configuration.
	var err := _replay.load(_path)
	if err != OK:
		printerr("load %s failed (error %d)" % [_path, err])
		for line in _replay.warnings():
			printerr("  " + line)
		return false
	if _replay.get_tick_count() < _ticks:
		printerr("the recording is %d ticks and this run wants %d" % [
			_replay.get_tick_count(), _ticks])
		return false

	# **The interval comes from the recording, not from this run's arguments.**
	#
	# It is a header field, so the recording decides which ticks have a checkpoint.
	# A playback that hashed on its own schedule would ask for a recorded hash at a
	# tick the footer has none for, and `compare_checkpoint` reports that as a
	# divergence -- a determinism alarm caused entirely by a flag mismatch. Taken
	# rather than validated, because there is nothing a caller could usefully do
	# with the error that this does not do for them.
	if _replay.get_hash_interval() != _interval:
		_notes.append("interval %d taken from the recording, not the %d asked for"
			% [_replay.get_hash_interval(), _interval])
		_interval = _replay.get_hash_interval()

	# The sabotages that act on the file or on the configuration, applied before
	# admission because that is where each of them has to be caught.
	if _break == "truncate":
		# One tick off the end. `replay_body_is_complete` compares the bytes on hand
		# against what the header declares, so this is the smallest possible lie.
		_replay.truncate_body(KartReplay.INPUT_BYTES)
		_notes.append("truncated the body by %d bytes" % KartReplay.INPUT_BYTES)
	if _break == "config":
		# A tick rate the recording was not made at. Since #174 this is inside
		# `config_hash`, so it must refuse naming `tick_hz` rather than diverge.
		session.set_tick_hz(Engine.physics_ticks_per_second * 2)
		_notes.append("live tick_hz moved to %d" % session.get_tick_hz())
	if _break == "input":
		# One code of one axis: 1.53e-5 of throttle. The smallest change the format
		# can express, and the point is that it is still caught.
		var before: Dictionary = _replay.input_at(_break_tick, 0)
		for _i in _break_codes:
			if not _replay.perturb_input(_break_tick, 0, 0):
				printerr("could not perturb tick %d" % _break_tick)
				return false
		var after: Dictionary = _replay.input_at(_break_tick, 0)
		_notes.append("moved throttle at tick %d by %d code(s): %.9f -> %.9f (delta %.9f)"
			% [_break_tick, _break_codes, before["throttle"], after["throttle"],
				after["throttle"] - before["throttle"]])

	if not _replay.begin_playback(session):
		# Refused. That is a verdict, not a crash, and for two of the sabotages it
		# is the *correct* verdict -- so the report is printed rather than bailed
		# out of. `_report` decides whether a refusal is a pass or a failure.
		return true
	return true


## The scenario, as a pure function of the tick counter.
##
## Times are in ticks and nothing here reads a clock. `mixed` exercises every axis
## the format stores and both shift edges, because a recording that only ever moved
## the throttle would round-trip nine bytes of which five were always zero and
## would prove nothing about the other four.
func _program(tick: int) -> Dictionary:
	var second := float(tick) / float(Engine.physics_ticks_per_second)
	match _scenario:
		"accel":
			return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
		"brake":
			if second < 6.0:
				return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
			return {"throttle": 0.0, "brake": 1.0, "steer": 0.0}
		"mixed":
			# A lap-shaped program: launch, up through the gearbox, brake, a long
			# left, a long right, a clutch dip and a downshift, then power out. The
			# steering values are deliberately *not* round numbers — a value that
			# lands exactly on the quantization grid would round-trip even if the
			# grid arithmetic were wrong, and every axis here has a fractional part
			# that does not.
			if second < 0.5:
				return {"throttle": 0.0, "brake": 0.0, "steer": 0.0, "clutch": 1.0}
			if second < 3.0:
				# Launch. One shift edge at a time: `shift_up` is true for exactly
				# the tick the request is made, because a level makes a held button
				# shift once per tick through the whole gearbox.
				return {
					"throttle": 0.97531, "brake": 0.0, "steer": 0.01379,
					"clutch": maxf(0.0, 1.0 - (second - 0.5) * 2.0),
					"shift_up": tick % 96 == 0,
				}
			if second < 6.0:
				return {"throttle": 1.0, "brake": 0.0, "steer": -0.03571,
					"shift_up": tick % 96 == 0}
			if second < 8.0:
				return {"throttle": 0.0, "brake": 0.86420, "steer": 0.0,
					"shift_down": tick % 48 == 0}
			if second < 13.0:
				return {"throttle": 0.41237, "brake": 0.0, "steer": 0.31415}
			if second < 18.0:
				return {"throttle": 0.41237, "brake": 0.0, "steer": -0.27182}
			if second < 20.0:
				return {"throttle": 0.0, "brake": 0.31415, "steer": 0.11235,
					"clutch": 0.66667, "shift_down": tick % 48 == 0}
			return {"throttle": 0.85137, "brake": 0.0, "steer": 0.07182,
				"shift_up": tick % 120 == 0}
		_:
			return {"throttle": 0.0, "brake": 0.0, "steer": 0.0}


## One tick of intent, and the only place input crosses into the vehicle.
##
## **Snapped before anything sees it.** The identical Dictionary goes to `KartBody`
## and to the recorder, so the live run and the replay consume bit-identical
## doubles by construction rather than by hope. This is ADR-0041's central
## decision made mechanical: there is no code path here that could hand the solver
## one value and the file another.
func _scripted_input() -> Dictionary:
	# The run is over and the report has been taken. Anything the engine asks for
	# now is past the end of the recording, so it gets neutral input and changes
	# nothing -- recording it would make the body one tick longer than the header.
	if _done:
		return {}

	var index := _input_calls
	_input_calls += 1
	# **The Callable must be consulted exactly once per tick, in step with the
	# hash**, and the expected index is `_tick - 1` rather than `_tick`.
	#
	# `_physics_process` above runs before node propagation, so by the time the
	# vehicle asks for tick T's input, `_sample` has already hashed tick T and
	# advanced the counter. The two are still in step -- hash[T] is the state
	# entering tick T and input[T] is what is consumed from it -- and this
	# assertion is what proves that rather than assuming it. It is checked because
	# a Callable consulted twice in a frame, or skipped in one, would make every
	# number after it describe a different tick than it claims to.
	if index != _tick - 1:
		_fail("the input Callable ran %d times by tick %d; the stream is out of step"
			% [_input_calls, _tick])
		return {}

	if _mode == "play":
		var stored: Dictionary = _replay.input_at(index, 0)
		if stored.is_empty():
			_fail("tick %d decoded to nothing" % index)
			return {}
		return stored

	if index < SETTLE_TICKS:
		var settling := KartReplay.snap_input({})
		_record(settling)
		return settling

	var raw := _program(index - SETTLE_TICKS)
	if _break == "grid" and index == _break_tick:
		# **The negative control for the refusal ADR-0041 exists for.** An off-grid
		# value handed straight to the recorder, with no snap. `record_input` must
		# refuse it; if it rounded instead, the live run would consume this value
		# and the replay would consume a different one.
		raw["throttle"] = 0.1234567890123
		_notes.append("handed tick %d an off-grid throttle of %.13f"
			% [index, raw["throttle"]])
		if _replay.record_input(raw):
			_fail("the recorder ACCEPTED off-grid input at tick %d" % index)
		else:
			_notes.append("the recorder refused it, which is the required behavior")
			# Recorded properly so the run can finish and the refusal is the only
			# thing under test.
			_record(KartReplay.snap_input(raw))
		return KartReplay.snap_input(raw)

	var snapped: Dictionary = KartReplay.snap_input(raw)
	_record(snapped)
	return snapped


func _record(input: Dictionary) -> void:
	if not _replay.record_input(input):
		_fail("the recorder refused tick %d" % _tick)


func _fail(why: String) -> void:
	if _failed:
		return
	_failed = true
	printerr("FAIL: " + why)


func _physics_process(_delta: float) -> bool:
	# **`quit()` schedules a quit; it does not stop the loop where it is called.**
	# So `_initialize` returning early -- `--mode=scrub`, an unloadable scene, a
	# missing class -- still lets one physics frame run, and that frame walked into
	# `_attach` with a null scene and printed "could not attach" over a scrub that
	# had worked perfectly. There is nothing to do on a frame with no scene.
	if _root == null:
		return true
	if _kart == null and not _attach():
		printerr("could not attach")
		quit(1)
		return true
	if _failed:
		_report()
		return true
	# A refused playback has nothing to simulate: the question was never asked, and
	# running 3,000 ticks to confirm it would be 3,000 ticks of arithmetic nobody
	# is going to read.
	if _mode == "play" and _replay.verdict() == KartReplay.VERDICT_REFUSED:
		_report()
		return true

	# **The stop is checked before the sample, not after, and the off-by-one it
	# fixes is the whole reason this comment exists.**
	#
	# The order within one physics frame is: this function, then node propagation.
	# So `_sample` hashes the state *entering* tick T, and the vehicle then asks
	# `_scripted_input` for tick T's input during the same frame. Sampling first and
	# stopping afterwards ended the run between those two halves — it hashed tick
	# N-1 and quit before the input for tick N-1 was ever recorded, leaving N
	# checkpoints over N-1 ticks. `finalize` caught it, which is what it is for, but
	# the cause is here.
	if _tick >= _ticks:
		_report()
		return true

	_sample()
	_tick += 1
	return false


## One tick's hashing, measurement, and — under `--break=state` — sabotage.
##
## This runs before node propagation, so the state hashed at tick T is the state
## *entering* tick T, before the vehicle has consumed tick T's input. Both the
## recording and the playback hash at the same point, which is the only property
## that has to hold for the comparison to mean anything.
func _sample() -> void:
	var position := _kart.global_position

	if _tick % _interval == 0:
		_hash.add_int(_tick)
		_hash.add_transform(_kart.global_transform)
		_hash.add_vector3(_kart.linear_velocity)
		_hash.add_vector3(_kart.angular_velocity)
		var digest := _hash.hex()
		_sequence.append(digest)
		if _mode == "record":
			if not _replay.record_checkpoint(_tick, digest):
				_fail("checkpoint %d refused" % _tick)
		else:
			_replay.compare_checkpoint(_tick, digest)

	# The state sabotage, applied *after* this tick's hash so that the checkpoint
	# at `_break_tick` is still clean and the divergence shows up at the next one.
	# That makes the reported tick the first tick the two runs actually differ at,
	# which is what the report claims it is.
	if _break == "state" and _tick == _break_tick and not _sabotage_applied:
		_sabotage_applied = true
		_kart.linear_velocity += Vector3(STATE_NUDGE_MS, 0.0, 0.0)
		_notes.append("nudged the kart %.4f m/s sideways at tick %d"
			% [STATE_NUDGE_MS, _break_tick])

	_distance += position.distance_to(_previous_position)
	_previous_position = position
	_top_speed = maxf(_top_speed, _kart.linear_velocity.length())


func _report() -> void:
	if _done:
		return
	_done = true
	var lines: Array[String] = []
	var seconds := float(_tick) / float(Engine.physics_ticks_per_second)
	lines.append("--- replay %s: %d ticks, %.2f s" % [_mode, _tick, seconds])
	lines.append("    scenario        %s on %s" % [_scenario, _scene_path.get_file()])
	lines.append("    distance        %8.1f m" % _distance)
	lines.append("    top speed       %8.1f km/h" % KartCore.ms_to_kmh(_top_speed))

	# **The stall ratio, reported rather than trusted.**
	#
	# `max_physics_steps_per_frame` clamps and does not bank: under a stall Godot
	# runs its 8 ticks and stops, so simulation time falls behind wall-clock — 0.6476
	# of real time, measured — and a **tick-counting replay cannot see it**. That is
	# the trap, and the honest statement of it is that it does not threaten this
	# gate: every index here is a tick, so a stalled run replays identically. What it
	# would do is let this report claim 25 s of driving that took 40 s of wall clock,
	# so the number is printed and never inferred.
	var ratio := _kart.get_time_ratio()
	if ratio < 0.0:
		lines.append("    time ratio         unmeasured (the run was shorter than one window)")
	else:
		lines.append("    time ratio      %8.4f simulated seconds per wall second%s" % [
			ratio, "   STALLED" if ratio < 0.9 else "",
		])

	# Stale-input ticks. ADR-0040's one failure mode, and a replay's worst one: a
	# tick that consumed neutral input because the producer did not run is a tick
	# the recording says something else about.
	var stale := _kart.get_stale_input_ticks()
	if stale > 0:
		lines.append("    stale input     %8d ticks ran on neutral input" % stale)
		_fail("%d ticks consumed neutral input; the recording does not describe them" % stale)
	if _input_calls != _tick:
		lines.append("    input calls     %8d against %d ticks" % [_input_calls, _tick])

	if _mode == "record":
		_report_record(lines)
	else:
		_report_play(lines)

	for note in _notes:
		lines.append("    note            %s" % note)

	print("\n".join(lines))
	_finish()


func _report_record(lines: Array[String]) -> void:
	if _sequence.is_empty():
		_fail("nothing was recorded")
		return
	if not _replay.finalize():
		_fail("finalize refused")
		return
	var err := _replay.save(_path)
	for line in _replay.warnings():
		lines.append("    warning         %s" % line)
	if err != OK:
		_fail("save %s failed (error %d)" % [_path, err])
		return
	var size := 0
	var file := FileAccess.open(_path, FileAccess.READ)
	if file != null:
		size = file.get_length()
		file.close()
	lines.append("    recorded        %d ticks, %d karts, interval %d" % [
		_replay.get_tick_count(), _replay.get_kart_count(), _replay.get_hash_interval()])
	lines.append("    checkpoints     %8d" % _replay.checkpoint_count())
	lines.append("    file            %s   %d bytes  (%.2f bytes/tick)" % [
		_path, size, float(size) / maxf(1.0, float(_replay.get_tick_count()))])
	lines.append("    config          %s" % _replay.config_hash_hex())
	lines.append("    build           %s   api %s" % [
		_replay.get_build(), _replay.get_api_version()])
	_print_sequence(lines)
	# The last hash on its own line, so a shell can grep for it exactly the way
	# `drive.sh` greps `drive_probe.gd`'s.
	lines.append("state-hash %s" % _sequence[_sequence.size() - 1])


func _report_play(lines: Array[String]) -> void:
	lines.append("    verdict         %s" % _replay.verdict_name())
	lines.append("    %s" % _replay.describe())
	if _replay.build_warning() or _replay.api_warning():
		lines.append("    (warnings do not change a verdict; cross-build determinism is not"
			+ " claimed)")

	if _replay.verdict() == KartReplay.VERDICT_DIVERGED:
		# The two hashes and the tick, which is what makes this actionable rather
		# than a boolean. At interval 1 the tick is exact.
		lines.append("    FIRST DIVERGENCE at tick %d  (%.3f s)" % [
			_replay.diverged_tick(),
			float(_replay.diverged_tick()) / float(Engine.physics_ticks_per_second)])
		lines.append("      recorded  %s" % _replay.recorded_hash_hex())
		lines.append("      live      %s" % _replay.live_hash_hex())
		if _interval > 1:
			lines.append("      (interval %d, so the cause is in the %d ticks before this;"
				% [_interval, _interval] + " re-run with --interval=1)")
	lines.append("    compared        %8d checkpoints" % _replay.checkpoints_compared())
	_print_sequence(lines)
	if _sequence.size() > 0:
		lines.append("state-hash %s" % _sequence[_sequence.size() - 1])


## A readable slice of the hash sequence. The whole of it is thousands of lines, so
## the head, the tail, and the divergence if there is one.
func _print_sequence(lines: Array[String]) -> void:
	if _sequence.is_empty():
		return
	lines.append("    hash sequence   %d entries, interval %d" % [_sequence.size(), _interval])
	var shown: Array[int] = []
	for i in mini(4, _sequence.size()):
		shown.append(i)
	var diverged := int(_replay.diverged_tick()) / _interval
	if _replay.verdict() == KartReplay.VERDICT_DIVERGED and diverged < _sequence.size():
		for i in range(maxi(0, diverged - 1), mini(_sequence.size(), diverged + 2)):
			if not shown.has(i):
				shown.append(i)
	for i in range(maxi(0, _sequence.size() - 3), _sequence.size()):
		if not shown.has(i):
			shown.append(i)
	shown.sort()
	var previous := -1
	for i in shown:
		if previous >= 0 and i != previous + 1:
			lines.append("      ...")
		lines.append("      tick %6d  %s" % [i * _interval, _sequence[i]])
		previous = i


## Exit, with `--break`'s inverted rule applied.
##
## **A sabotaged run that passes is the failure.** The verdict has to be the
## saboteur's own fingerprint and not any red that happened to be lying around:
## `--break=config` must be refused *naming tick_hz*, `--break=truncate` must be
## refused *for a truncated body*, and the two that corrupt the simulation must
## DIVERGE rather than refuse — a refusal there would mean the run never happened
## and the sabotage was never exercised.
func _finish() -> void:
	if _break.is_empty():
		quit(1 if _failed else 0)
		return

	var caught := false
	var wanted := ""
	match _break:
		"grid":
			wanted = "the recorder refuses off-grid input"
			caught = not _failed
		"input", "state":
			wanted = "a diverged verdict"
			caught = _replay.verdict() == KartReplay.VERDICT_DIVERGED
		"config":
			wanted = "refused, naming tick_hz"
			caught = _replay.verdict() == KartReplay.VERDICT_REFUSED \
				and _replay.refusal_reason() == KartReplay.REFUSAL_CONFIG_MISMATCH \
				and _replay.describe().contains("tick_hz")
		"truncate":
			wanted = "refused, for a truncated body"
			caught = _replay.verdict() == KartReplay.VERDICT_REFUSED \
				and _replay.refusal_reason() == KartReplay.REFUSAL_TRUNCATED_BODY
		_:
			printerr("unknown --break=%s" % _break)
			quit(1)
			return

	if caught:
		print("break %-9s CAUGHT   (wanted %s)" % [_break, wanted])
	else:
		print("break %-9s ESCAPED  (wanted %s, got %s)" % [
			_break, wanted, _replay.verdict_name()])
	# Inverted: a caught sabotage is a successful negative control.
	quit(0 if caught else 1)


## Take back everything the probe wrote, as its own mode.
##
## **Not done at the end of a run**, and that is deliberate rather than lazy: a
## record run's whole output is the file the next process reads, and a play run
## may be one of six sabotages replayed against the same recording. A probe that
## tidied up after itself would delete the subject of the next step. `replay.sh`
## calls this once, last.
##
## It matters that something does. Every worktree shares one `user://` — it is
## keyed on `application/config/name`, so this directory is the same directory for
## the main checkout and every agent at once.
static func scrub() -> void:
	var dir := DirAccess.open(PRIVATE_DIR)
	if dir == null:
		return
	for name in dir.get_files():
		DirAccess.remove_absolute(PRIVATE_DIR + "/" + name)
	DirAccess.remove_absolute(PRIVATE_DIR)
