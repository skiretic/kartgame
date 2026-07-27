extends SceneTree

## The ghost gate — ROADMAP M3c, `src/core/ghost.h`, `src/session/kart_ghost.h`.
##
##     godot --headless --path . --script tools/verify/ghost_probe.gd
##       --skip-drive   leave out the one check that loads a scene and drives the
##                      real kart, for a fast run while iterating
##
## Every check prints its measurement rather than a bare PASS, for
## `contact_probe.gd`'s reason: a gate whose output is twenty identical words tells
## a reader nothing about what changed between two runs of it.
##
## ## What is actually being defended
##
## `src/core/ghost.h` was written with no engine to test against. Its arithmetic has
## unit tests; what it has never had is a real kart's transform stream going in and a
## file coming out. Four of its properties are load-bearing and none of them is
## visible in a render:
##
##   1. **The interpolation passes through its samples.** A sample is a place the
##      kart actually was, and a curve that missed it draws a ghost through the
##      scenery on a hairpin. Checked against an interpolator-free read of the same
##      sample, at every sample, not at one.
##   2. **The endpoint tangents are reflected, not duplicated.** `ghost.h` records
##      that duplicating them bowed a straight line by 0.52 m — most of a kart's
##      width — and a best lap is a *flying* lap, so the ends are at racing speed
##      across the line and not at a standing start. Measured on a straight line laid
##      out on the millimeter grid, where the exact answer is zero and any deviation
##      at all is the interpolator.
##   3. **A ghost survives a file.** Every sample bit-identical, and a body that
##      disagrees with its header refused rather than half-drawn.
##   4. **An invalid lap is not saved at all.** `GAMEDESIGN.md` §3 puts lap
##      validation next to the ghost in Practice, and a best lap set with two wheels
##      on the grass is not a line worth chasing.
##
## ## Why the truth is a circle and a straight line
##
## A lap is not a truth. Eyeballing a recorded lap's interpolation proves that it
## looks smooth, which is what a wrong interpolator also looks like. So the paths
## here are geometry with a closed form: a 30 m circle at 20 m/s, which is exactly
## the corner `ghost.h` sized the 30 Hz sample rate against — `test_track.tscn`'s T3
## sweeper — and a straight line at 21 m/s, which puts every sample on the storage
## grid so the quantization term is exactly zero rather than merely small.
##
## The circle is compared two ways, and the second one is the measurement that
## matters. Against the analytic circle, Catmull-Rom's own error is **invisible**:
## `ghost.h` computes it at 0.005 mm and the 1 mm position grid contributes up to
## 1.08 mm through the interpolation, so the total is quantization and nothing else.
## What separates a Catmull-Rom from a lerp is therefore measured by running a linear
## reference over the *same stored samples* and comparing both against the same
## analytic truth — the difference is the sagitta, `a h^2 / 8`, which is the number
## `ghost.h`'s rate argument is built on and which this file predicts before it
## measures.

const SCENE_PATH := "res://scenes/game/test_track.tscn"

## Scratch for the round-trip files. Under `user://` for `tuning_probe.gd`'s reason:
## a verification tool that writes into the repository is one `git add -A` away from
## committing its own temporary files, and CLAUDE.md records that happening twice.
const SCRATCH_DIR := "user://ghost_probe"

## The sweeper `ghost.h` sized 30 Hz against: `test_track.tscn`'s T3, 30 m radius,
## and §6.4 puts the kart near 20 m/s there. 13.33 m/s^2 of lateral.
const CIRCLE_RADIUS := 30.0
const CIRCLE_SPEED := 20.0

## 21 m/s is 0.7 m per sample at 30 Hz, which is an exact number of millimeters, so
## the straight line's samples land on the storage grid with no rounding at all and
## the expected interpolation error is exactly zero.
const LINE_SPEED := 21.0

## The physics rate `project.godot` runs at, which is the rate a recorder is fed.
const PHYSICS_HZ := 120.0

## The position grid, meters. `ghost.h`: int32 at 1 mm.
const POSITION_QUANTUM := 0.001

## Half the angle grid, radians. -pi..pi over 32,767 codes.
const HALF_ANGLE_QUANTUM := PI / 32767.0 * 0.5

## One step of a float32 mantissa, relative. 2^-23.
##
## **Godot is a single-precision build**: `real_t` is `float`, so `Vector3`,
## `Basis` and `Transform3D` all carry 32-bit components, and every quantity this
## file reads back through one of them is rounded to about seven significant digits.
## `ghost.h` stores position as an int32 at 1 mm and works in double internally, so
## nothing about the format loses anything to this — at 200 m the engine's own grain
## is 15 um, which is 65 times finer than the storage quantum. What it does mean is
## that a check asserting double precision *through the bridge* is asserting
## something the engine cannot deliver. Two of the bounds here were written at 1e-12
## and failed for exactly that reason, at 7.2e-08 and 2.3e-06, both of which are this
## number times the magnitude involved.
##
## So the two bounds that read a large coordinate back out are derived from this and
## from the path length rather than picked, and the ones that measure a quantity which
## is genuinely zero — the cross-track deviation in check 6 — stay tight, because zero
## has no magnitude to lose.
const FLOAT32_GRAIN := 1.1920929e-7

## The most a Catmull-Rom can amplify the error already in its four samples.
##
## `sum |w_i(t)|` for the tension-1/2 basis is `1 + t(1 - t)`, because `w0 + w3` is
## exactly `-t(1 - t)/2` and `w1 + w2` is `1 - (w0 + w3)`. It peaks at **1.25** at the
## midpoint of a segment. So an interpolated coordinate can sit 1.25 half-quanta from
## the truth even when every sample is within half a quantum of it, and a bound of one
## half-quantum would fail on a correct interpolator.
const SPLINE_WEIGHT_L1 := 1.25

## Ticks of settling before the driven check starts recording. `drive_probe.gd` waits
## the same out: the kart is spawned above the ground and dropped, so the first
## fraction of a second is a landing and not driving.
const SETTLE_TICKS := 120

## Ticks recorded off the real kart. 601 rather than 600 — see check 3: a lap of `L`
## ticks is `L + 1` calls, one at the line on the way in and one on the way out.
const DRIVE_TICKS := 601

var _lines: Array[String] = []
var _passed := 0
var _failed := 0

var _skip_drive := false
var _root: Node
var _kart: KartBody
var _tick := 0
var _attached := false
var _recorder: KartGhost
var _truth: Array[Transform3D] = []

## Set by `_report_and_quit`. `quit()` asks the loop to stop, it does not stop the
## frame in progress, so without this the skip-drive path runs one `_physics_process`
## against a scene it never instantiated and reports a null dereference over a clean
## table of results.
var _done := false


func _initialize() -> void:
	var args := Cmdline.parse()
	_skip_drive = Cmdline.as_bool(args, "skip-drive", false)

	if not ClassDB.class_exists("KartGhost"):
		printerr("KartGhost is not registered — build the extension: scons arch=arm64 target=editor")
		quit(1)
		return

	_clean_scratch()

	_lines.append("=== ghost gate ========================================================")
	_lines.append("")

	_check_constants()
	_check_basis_round_trip()
	_check_sampling_stride()
	_check_exact_at_samples()
	_check_transform_round_trip()
	_check_straight_line_is_straight()
	_check_circle_against_analytic()
	_check_file_round_trip()
	_check_bytes_per_lap()
	_check_invalid_lap_is_not_saved()
	_check_header_body_disagreement()
	_check_future_format_refused()
	_check_id_scheme()
	_check_one_file_per_slot()
	_check_comparability()

	if _skip_drive:
		# Both need a live tree: one needs the scene and the kart in it, the other
		# needs `GhostKart` parented so its `_ready` has run.
		_lines.append("check %-38s SKIP   --skip-drive" % "a real kart records what it did")
		_lines.append("check %-38s SKIP   --skip-drive" % "the ghost kart is a visual that moves")
		_report_and_quit()
		return

	var packed: PackedScene = load(SCENE_PATH)
	if packed == null:
		_ok("a real kart records what it did", false, "could not load " + SCENE_PATH)
		_report_and_quit()
		return
	_root = packed.instantiate()
	get_root().add_child(_root)


# --- the driven check -----------------------------------------------------------
#
# A `--script` main loop's scene is not in the tree during `_initialize`, so the
# nodes are looked up on the first `_physics_process`. `drive_probe.gd` and
# `shoot.gd` both do this.


func _physics_process(_delta: float) -> bool:
	if _done:
		return true
	if not _attached:
		_kart = _root.find_child("Kart", false, false) as KartBody
		if _kart == null:
			_ok("a real kart records what it did", false, "the scene has no Kart")
			_report_and_quit()
			return true
		# Open loop, and held: a fixed function of nothing at all. A closed loop would
		# measure a controller, and two runs of a controller reading its own state are
		# not guaranteed to agree — `drive_probe.gd` makes the same argument at length.
		_kart.input_driver = func() -> Dictionary:
			return {"throttle": 0.6, "brake": 0.0, "steer": 0.12}
		_attached = true

	_tick += 1
	if _tick < SETTLE_TICKS:
		return false
	if _tick == SETTLE_TICKS:
		_recorder = KartGhost.new()
		_recorder.set_track("test_track")
		_recorder.set_track_hash_hex("0x00000000000000ab")
		_recorder.set_layout(0)
		_recorder.set_kart_class(1)
		_recorder.begin_record()

	# Recorded and captured on the same tick, from the same read of the same
	# transform, so the comparison below is the recorder's arithmetic and not two
	# different reads of a moving kart.
	var here := _kart.global_transform
	_truth.append(here)
	_recorder.record_tick(here)

	if _tick < SETTLE_TICKS + DRIVE_TICKS - 1:
		return false

	_check_driven_recording()
	_check_ghost_kart_is_a_visual()
	_report_and_quit()
	return true


## 17. `scripts/game/ghost_kart.gd` loads, moves, and is not in the physics path.
##
## An unexercised script is a script that does not work, and this project has the
## receipts: CLAUDE.md's own control list advertised four bindings that reached
## nothing, and `#169` exists because a headless gate went round the InputMap
## entirely. `GhostKart` is the only part of this work a driver actually sees and
## nothing else in this file touches it, so it gets loaded, handed a real ghost, and
## asked to move.
##
## The physics assertion is the one that matters and it is structural rather than a
## matter of taste. ADR-0041: a ghost is not re-simulated, because re-simulating one
## buys nothing and costs a second vehicle solve every tick. A `CollisionObject3D`
## anywhere in that subtree would also mean a time trial against yourself is a
## collision with yourself.
func _check_ghost_kart_is_a_visual() -> void:
	var problems: Array[String] = []
	var script: GDScript = load("res://scripts/game/ghost_kart.gd")
	if script == null:
		_ok("the ghost kart is a visual that moves", false, "ghost_kart.gd did not load")
		return
	var kart: Node3D = script.new()
	get_root().add_child(kart)

	# Nothing to draw yet, so nothing is drawn. A ghost kart parked at the origin on
	# the start line reads as a stalled rival.
	if kart.visible:
		problems.append("it was visible before it had a ghost")
	if kart.has_ghost():
		problems.append("it claimed a ghost it was never given")

	kart.set_ghost(_recorder)
	if not kart.has_ghost():
		problems.append("it refused a complete ghost")
	if not kart.visible:
		problems.append("it stayed hidden with a ghost loaded")
	# Placed on the way in rather than a frame later, or a ghost flashes at the origin
	# on every lap.
	if kart.transform.origin.distance_to(_recorder.transform_at_time(0.0).origin) > 1e-4:
		problems.append("it did not place itself when it was given the ghost")

	# It follows the stream. Driven through the public clock, which is what a session
	# runner sets — `autoplay` is for a still.
	var span: float = kart.playback_duration()
	if absf(span - _recorder.playback_duration()) > 1e-9:
		problems.append("it reports a %f s stream against the ghost's %f" % [
			span, _recorder.playback_duration(),
		])
	var worst := 0.0
	var travelled := 0.0
	var previous := kart.transform.origin
	for step in 41:
		var time: float = span * float(step) / 40.0
		kart.playback_time = time
		# `_process` is the only place this node moves. Called directly rather than
		# waited for, because a `--script` loop's idle frames are not guaranteed here
		# and the point is that it is `_process` and not `_physics_process`.
		kart._process(0.0)
		worst = maxf(worst, kart.transform.origin.distance_to(
			_recorder.transform_at_time(time).origin))
		travelled += previous.distance_to(kart.transform.origin)
		previous = kart.transform.origin
	if worst > 1e-4:
		problems.append("it strayed %f m from the stream it was given" % worst)
	if travelled < 20.0:
		problems.append("it only moved %.2f m over the whole ghost" % travelled)

	# And the structural assertion. No body, no shape, no solver, anywhere under it.
	var physics: Array[String] = []
	var meshes := 0
	for node in _subtree(kart):
		if node is CollisionObject3D or node is CollisionShape3D or node is KartBody:
			physics.append(node.get_class())
		if node is MeshInstance3D:
			meshes += 1
	if not physics.is_empty():
		problems.append("it carries %s" % ", ".join(physics))
	if kart.has_method("_physics_process"):
		problems.append("it has a _physics_process")

	kart.set_ghost(null)
	if kart.visible:
		problems.append("it stayed visible after its ghost was taken away")

	_ok("the ghost kart is a visual that moves", problems.is_empty(),
		"%d meshes, no physics, %.2f m along a %.2f s stream, %s m off" % [
			meshes, travelled, span, _sci(worst),
		] if problems.is_empty() else "; ".join(problems))
	kart.queue_free()


func _subtree(root: Node) -> Array[Node]:
	var found: Array[Node] = []
	var pending: Array[Node] = [root]
	while not pending.is_empty():
		var node: Node = pending.pop_back()
		found.append(node)
		for child in node.get_children():
			pending.append(child)
	return found


## 16. The recording path, against a real kart rather than a synthetic stream.
##
## This is the check no unit test can replace, and what it is really asking is
## whether `ghost.h`'s sampling survives contact with a `RigidBody3D`'s transform: a
## basis that is only nearly orthonormal, a yaw that accumulates past pi, a position
## tens of meters from the origin, and a suspension that is moving the attitude every
## tick.
func _check_driven_recording() -> void:
	var expected := int(ceil(float(DRIVE_TICKS) / float(KartGhost.TICKS_PER_SAMPLE)))
	var counted := _recorder.sample_count()
	_ok("a real kart samples on the stride", counted == expected,
		"%d samples from %d ticks, expected %d" % [counted, DRIVE_TICKS, expected])

	# Every sample against the transform the kart was actually at on that tick. The
	# tolerance is the storage grid and nothing looser: half a millimeter per axis and
	# half an angle code.
	var worst_position := 0.0
	var worst_angle := 0.0
	for i in counted:
		var truth: Transform3D = _truth[i * KartGhost.TICKS_PER_SAMPLE]
		var sample := _recorder.sample_at_index(i)
		var position: Vector3 = sample["position"]
		worst_position = maxf(worst_position, (position - truth.origin).length())
		var euler := truth.basis.orthonormalized().get_euler(EULER_ORDER_YXZ)
		worst_angle = maxf(worst_angle, absf(_arc(float(sample["yaw"]), euler.y)))
		worst_angle = maxf(worst_angle, absf(_arc(float(sample["pitch"]), euler.x)))
		worst_angle = maxf(worst_angle, absf(_arc(float(sample["roll"]), euler.z)))
	var position_bound := POSITION_QUANTUM * 0.5 * sqrt(3.0)
	_ok("a real kart's samples are on the grid",
		worst_position <= position_bound and worst_angle <= HALF_ANGLE_QUANTUM * 1.001,
		"%.6f mm of %.6f, %s rad of %s" % [
			worst_position * 1000.0, position_bound * 1000.0,
			_sci(worst_angle), _sci(HALF_ANGLE_QUANTUM),
		])

	var travelled := (_truth[_truth.size() - 1].origin - _truth[0].origin).length()
	_ok("the kart actually drove", travelled > 20.0, "%.2f m over %.2f s" % [
		travelled, float(DRIVE_TICKS) / PHYSICS_HZ,
	])

	# And through a file, because the driven stream is the one whose bytes have never
	# been round-tripped.
	var lap := float(DRIVE_TICKS - 1) / PHYSICS_HZ
	var closed := _recorder.finish_record(lap, PackedFloat64Array())
	var path := SCRATCH_DIR + "/driven.ghost"
	var saved := _recorder.save(path) if closed else FAILED
	var reloaded := KartGhost.new()
	var report := reloaded.load(path) if saved == OK else {}
	var identical := bool(report.get("ok", false)) \
			and reloaded.sample_count() == _recorder.sample_count()
	if identical:
		for i in _recorder.sample_count():
			if not _samples_identical(_recorder.sample_at_index(i), reloaded.sample_at_index(i)):
				identical = false
				break
	_ok("a real kart's ghost survives a file", identical, "%d samples, %d bytes on disk" % [
		reloaded.sample_count(), _file_size(path),
	])


# --- checks that need no scene ---------------------------------------------------


## 1. The class constants are `ghost.h`'s, and the rate is the one the rate argument
## was made about.
##
## Not a formality. Every measurement below is scaled by 30 Hz and 18 bytes; a bridge
## serving its own numbers would leave this whole file measuring something consistent
## and wrong.
func _check_constants() -> void:
	var problems: Array[String] = []
	if KartGhost.SAMPLE_HZ != 30:
		problems.append("sample_hz is %d" % KartGhost.SAMPLE_HZ)
	if KartGhost.TICKS_PER_SAMPLE != 4:
		problems.append("stride is %d" % KartGhost.TICKS_PER_SAMPLE)
	if KartGhost.SAMPLE_BYTES != 18:
		problems.append("a sample is %d bytes" % KartGhost.SAMPLE_BYTES)
	# The stride and the rate have to be the same statement about the same clock, or a
	# sample's index and its time disagree and the whole stream is stretched.
	if KartGhost.SAMPLE_HZ * KartGhost.TICKS_PER_SAMPLE != int(PHYSICS_HZ):
		problems.append("%d Hz on a stride of %d is not %d Hz" % [
			KartGhost.SAMPLE_HZ, KartGhost.TICKS_PER_SAMPLE, int(PHYSICS_HZ),
		])
	var configured := int(ProjectSettings.get_setting("physics/common/physics_ticks_per_second", 60))
	if configured != int(PHYSICS_HZ):
		problems.append("project.godot runs at %d Hz" % configured)
	_ok("the constants are ghost.h's", problems.is_empty(),
		"%d Hz, stride %d, %d bytes" % [
			KartGhost.SAMPLE_HZ, KartGhost.TICKS_PER_SAMPLE, KartGhost.SAMPLE_BYTES,
		] if problems.is_empty() else "; ".join(problems))


## 2. A basis survives the trip through three angles.
##
## The recorder stores orientation as yaw, pitch and roll — `ghost.h`'s choice, and
## it says why — which means `Basis.from_euler(b.get_euler(order), order)` has to
## reproduce `b`. Godot guarantees it for a rotation; this measures it, over a sweep
## including the seam at +/- pi and the roll and pitch a kart on a curb reaches. If it
## did not hold, every other check here would still pass and every ghost would be
## drawn at a slightly wrong attitude.
func _check_basis_round_trip() -> void:
	var worst := 0.0
	var worst_at := Vector3.ZERO
	for yaw_step in 17:
		for pitch_step in 5:
			for roll_step in 5:
				var euler := Vector3(
					deg_to_rad(-20.0 + 10.0 * pitch_step),
					-PI + TAU / 16.0 * yaw_step,
					deg_to_rad(-20.0 + 10.0 * roll_step),
				)
				var basis := Basis.from_euler(euler, EULER_ORDER_YXZ)
				var rebuilt := Basis.from_euler(basis.get_euler(EULER_ORDER_YXZ), EULER_ORDER_YXZ)
				for axis in 3:
					var error := (rebuilt[axis] - basis[axis]).length()
					if error > worst:
						worst = error
						worst_at = euler
	# A basis component is order 1, so the bound is a handful of float32 steps: the
	# trip is `from_euler`, three `atan2`/`asin`, and `from_euler` again, each rounding
	# once. 8 steps is 9.5e-07 and the measured figure is 7.2e-08, so this is loose
	# enough to be about the arithmetic and tight enough that a wrong Euler *order*
	# — which would be wrong by degrees, not by 1e-07 — still fails it.
	var bound := FLOAT32_GRAIN * 8.0
	_ok("a basis round-trips through three angles", worst < bound,
		"%s worst of %s, at yaw %.1f pitch %.1f roll %.1f deg" % [
			_sci(worst), _sci(bound),
			rad_to_deg(worst_at.y), rad_to_deg(worst_at.x), rad_to_deg(worst_at.z),
		])


## 3. The recorder samples every fourth tick, and the header count is the one the
## recorder counted — **not** the one `ghost_samples_for_lap` computes.
##
## A lap of `L` ticks is `L + 1` calls to `record_tick`: one at the line on the way in
## and one on the way out. So the count is `ceil((L + 1) / 4)` by definition, and that
## integer expression is what is asserted here, because it is the only statement of
## the stride that cannot itself be wrong in floating point.
##
## `ghost.h` also offers `ghost_samples_for_lap(t) = 1 + floor(t * 30)` for the same
## quantity, and **it does not always agree**. `lap_timing.h` builds a lap time as
## `lap_ticks * step_s_` with `step_s_ = 1.0 / 120.0`; the reciprocal is inexact, so
## the product lands a hair below the integer often enough that `floor` drops a
## sample. The sweep below tries the first 20,000 tick counts and prints the count;
## it comes out at **234**, and the division form `L / 120.0` is no refuge either at
## 98. Carried out to 200,000 ticks offline the figures are 1,961 and 813, so the rate
## is around 1% either way. The first failure is at **124 ticks**, which is one second
## of lap.
##
## This is why `KartGhost` writes what it counted and never derives the count from the
## lap time: a header that disagrees with its body by one sample is a ghost `load`
## refuses, so the whole best lap would be unreadable, roughly one lap in a hundred,
## for a reason nobody could see. The sweep is re-run in miniature below and the
## disagreement count is printed rather than asserted away — it is `ghost.h`'s
## behavior, not this class's, and the number is the point.
func _check_sampling_stride() -> void:
	var problems: Array[String] = []
	var step := 1.0 / PHYSICS_HZ
	# A bound constant arrives as Variant, so it is pinned to an `int` here: the
	# expression below is integer division and a Variant operand would make it float.
	var stride := int(KartGhost.TICKS_PER_SAMPLE)
	# Two of these — 124 and 492 — are lap lengths the helper gets wrong, and 492 it
	# gets wrong in both the multiplied and the divided form. They are in the list on
	# purpose: a gate that only tried round numbers is how this went unnoticed.
	var lengths: Array[int] = [124, 492, 1200, 1201, 1203, 2880, 5760, 6121]
	for lap_ticks in lengths:
		var ghost := _synthetic_line(lap_ticks + 1)
		var counted := ghost.sample_count()
		# The stride, as integers. `ceil((L + 1) / 4)`.
		var by_definition: int = (lap_ticks + 1 + stride - 1) / stride
		if counted != by_definition:
			problems.append("%d ticks: recorded %d, the stride says %d" % [
				lap_ticks, counted, by_definition,
			])
		# The header count is written by `finish_record` and not before, which is the
		# point: it is what the recorder counted, not what the lap time implies.
		ghost.set_track("stride")
		ghost.finish_record(float(lap_ticks) * step, PackedFloat64Array())
		if counted != int(ghost.header()["sample_count"]):
			problems.append("%d ticks: the header says %d against %d recorded" % [
				lap_ticks, int(ghost.header()["sample_count"]), counted,
			])
		# And the header a ghost that survived a file still agrees with its body, which
		# is the failure this immunity exists to prevent.
		var path := SCRATCH_DIR + "/stride.ghost"
		if ghost.save(path) != OK or not bool(KartGhost.new().load(path).get("ok", false)):
			problems.append("%d ticks: the ghost would not round-trip" % lap_ticks)
		DirAccess.remove_absolute(path)

	# The helper, measured rather than trusted. Not a pass condition: this is
	# `ghost.h`'s arithmetic and `KartGhost` does not use it for anything load-bearing.
	var multiplied := 0
	var divided := 0
	var first_bad := 0
	for lap_ticks in range(1, 20001):
		var by_definition: int = (lap_ticks + 1 + stride - 1) / stride
		if KartGhost.samples_for_lap(float(lap_ticks) * step) != by_definition:
			multiplied += 1
			if first_bad == 0:
				first_bad = lap_ticks
		if KartGhost.samples_for_lap(float(lap_ticks) / PHYSICS_HZ) != by_definition:
			divided += 1
	_ok("the recorder counts its own samples", problems.is_empty(),
		"8 lap lengths; samples_for_lap disagrees on %d of 20000 (ticks * step_s, first at %d) and %d (ticks / 120)" % [
			multiplied, first_bad, divided,
		] if problems.is_empty() else "; ".join(problems))


## 4. Interpolation is exact **at** the samples.
##
## An interpolator that is off at its own knots is a ghost that never quite passes
## through anywhere the kart was, and the error is small enough to look like smoothing
## rather than like a bug. Compared on the stored quantities rather than through a
## `Basis`, so that check 2's trig can neither hide a real difference nor invent one.
func _check_exact_at_samples() -> void:
	var ghost := _synthetic_circle(721)
	var hz := float(ghost.sample_hz())
	var worst_position := 0.0
	var worst_angle := 0.0
	for i in ghost.sample_count():
		var stored := ghost.sample_at_index(i)
		var played := ghost.sample_at_time(float(i) / hz)
		var here: Vector3 = played["position"]
		var there: Vector3 = stored["position"]
		worst_position = maxf(worst_position, (here - there).length())
		worst_angle = maxf(worst_angle, absf(_arc(float(played["yaw"]), float(stored["yaw"]))))
		worst_angle = maxf(worst_angle, absf(_arc(float(played["pitch"]), float(stored["pitch"]))))
		worst_angle = maxf(worst_angle, absf(_arc(float(played["roll"]), float(stored["roll"]))))
	_ok("playback is exact at the samples", worst_position < 1e-9 and worst_angle < 1e-9,
		"%s m, %s rad over %d samples" % [
			_sci(worst_position), _sci(worst_angle), ghost.sample_count(),
		])


## 5. `transform_at_index` reproduces the transform that went in, to the grid.
##
## The end-to-end statement of what a ghost costs in fidelity: a `Transform3D` in,
## quantized, a `Transform3D` out. The bound is the storage grid and nothing looser,
## because anything looser would pass on a recorder that had lost a component.
func _check_transform_round_trip() -> void:
	var ghost := KartGhost.new()
	ghost.set_track("grid_check")
	ghost.begin_record()
	var truth: Array[Transform3D] = []
	for tick in 401:
		var t := float(tick) / PHYSICS_HZ
		# Deliberately not on the grid, and deliberately off the origin: a position
		# 60 m out, a yaw past pi, and a few degrees of pitch and roll is what a kart on
		# a circuit actually hands over.
		var basis := Basis.from_euler(Vector3(
			deg_to_rad(2.5) * sin(t * 3.0),
			PI * 0.9 + t * 1.7,
			deg_to_rad(4.0) * cos(t * 2.0),
		), EULER_ORDER_YXZ)
		var here := Transform3D(basis, Vector3(
			61.2345 + 7.3 * sin(t), 0.1234 + 0.02 * cos(t * 5.0), -43.9876 + 9.1 * cos(t)))
		truth.append(here)
		ghost.record_tick(here)
	ghost.finish_record(400.0 / PHYSICS_HZ, PackedFloat64Array())

	var worst_position := 0.0
	var worst_angle := 0.0
	for i in ghost.sample_count():
		var recorded: Transform3D = truth[i * KartGhost.TICKS_PER_SAMPLE]
		var played := ghost.transform_at_index(i)
		worst_position = maxf(worst_position, (played.origin - recorded.origin).length())
		var a := recorded.basis.orthonormalized().get_euler(EULER_ORDER_YXZ)
		var b := played.basis.get_euler(EULER_ORDER_YXZ)
		for axis in 3:
			worst_angle = maxf(worst_angle, absf(_arc(b[axis], a[axis])))
	var position_bound := POSITION_QUANTUM * 0.5 * sqrt(3.0)
	_ok("a transform round-trips to the grid",
		worst_position <= position_bound and worst_angle <= HALF_ANGLE_QUANTUM * 1.001,
		"%.4f mm of %.4f, %s rad of %s" % [
			worst_position * 1000.0, position_bound * 1000.0,
			_sci(worst_angle), _sci(HALF_ANGLE_QUANTUM),
		])


## 6. A straight line interpolates exactly straight, at both ends.
##
## `ghost.h`'s 0.52 m bug: duplicating an endpoint gives a tangent of half the right
## magnitude, not a zero one, so the first and last segments bow away from the path by
## most of a kart's width. Reflecting — `p0 = 2*p1 - p2` — makes a straight run
## exactly straight, and a best lap is a **flying** lap, so both ends are at racing
## speed across the line and are the one place a driver is looking at it.
##
## 21 m/s at 30 Hz is 0.700 m per sample, an exact number of millimeters, so the
## quantization term here is not small — it is **zero**, and the expected deviation is
## therefore zero rather than a tolerance somebody chose. 1e-9 m is the double's own
## noise at 168 m from the origin with room to spare, and it is six orders below the
## storage quantum. The end segments are reported separately from the interior,
## because that is the only place the bug ever lived.
func _check_straight_line_is_straight() -> void:
	var ghost := _synthetic_line(241)
	var count := ghost.sample_count()
	var hz := float(ghost.sample_hz())
	var first: Vector3 = ghost.sample_at_index(0)["position"]
	var last: Vector3 = ghost.sample_at_index(count - 1)["position"]
	var along := (last - first).normalized()

	var worst_end := 0.0
	var worst_interior := 0.0
	var worst_spacing := 0.0
	var nominal := LINE_SPEED / hz / 8.0
	var previous := first
	for step in (count - 1) * 8 + 1:
		var index := float(step) / 8.0
		var here: Vector3 = ghost.sample_at_time(index / hz)["position"]
		var offset := here - first
		var deviation := (offset - along * offset.dot(along)).length()
		if int(floor(index)) == 0 or int(floor(index)) >= count - 2:
			worst_end = maxf(worst_end, deviation)
		else:
			worst_interior = maxf(worst_interior, deviation)
		if step > 0:
			# A bowed end segment is long as well as off-line, and a spacing that is not
			# uniform is a ghost that changes speed on the straight.
			worst_spacing = maxf(worst_spacing, absf((here - previous).length() - nominal))
		previous = here
	# The two deviation figures measure a quantity that is genuinely zero — how far
	# off-line the interpolation strays — so they keep a tight bound and both come out
	# at exactly 0.0. The spacing figure is the difference of two coordinates tens of
	# meters from the origin, read back through a float32 `Vector3`, so its floor is
	# the engine's grain at that magnitude and not the format's. Two roundings, one per
	# position.
	var span := (last - first).length()
	var spacing_bound := span * FLOAT32_GRAIN * 2.0
	_ok("a straight line is exactly straight",
		worst_end < 1e-9 and worst_interior < 1e-9 and worst_spacing < spacing_bound,
		"ends %s m, interior %s m, spacing %s m of %s over %d samples down %.1f m" % [
			_sci(worst_end), _sci(worst_interior), _sci(worst_spacing), _sci(spacing_bound),
			count, span,
		])


## 7. The circle, against the closed form, with a linear reference beside it.
##
## Four numbers and each answers a different question.
##
##   * **Catmull-Rom against the analytic circle, on the interior.** What a driver
##     actually sees, and it is dominated by the 1 mm position grid rather than by the
##     spline. `ghost.h` computes the spline term at 0.005 mm and the closed form for
##     this geometry puts it at 0.00017 mm; a coordinate on a 1 mm grid is up to
##     0.5 mm out and the spline amplifies that by at most `SPLINE_WEIGHT_L1`. So the
##     bound here is the quantization bound, and a number materially above it means
##     the interpolation is wrong.
##   * **A linear reference over the same stored samples.** The sagitta, `a h^2 / 8`,
##     predicted from the geometry before it is measured: 13.33 m/s^2 at 1/30 s is
##     1.852 mm. This is the number `ghost.h`'s whole rate argument rests on and it
##     has never been measured through the format.
##   * **The two against each other.** Their ratio is the only way to state that a
##     Catmull-Rom is being driven and not a lerp, because the grid hides the spline's
##     own error in the first number.
##   * **The end segments, against their own prediction — and this one found
##     something.** `ghost.h` reflects the phantom points, `p0 = 2*p1 - p2`, and says
##     that this "makes a straight run exactly straight and gives the right tangent at
##     the ends". The first half is true and check 6 measures it at zero. The second
##     half is not true on a **curve**: the reflection puts the phantom on the chord's
##     extension rather than on the circle, so it misses the true previous point by
##     `2R(1 - cos(w*h))` — 14.8 mm on this corner — and `w0(t)` carries a fraction of
##     that straight into the first and last segments.
##
##     In exact arithmetic that is a *huge* ratio — the closed form puts the interior
##     spline error at 0.00017 mm against 1.09 mm at the ends, six thousand times
##     worse. Through the format it is not, and the difference is the whole reason
##     both numbers are printed: the 1 mm storage grid contributes ~0.7 mm everywhere,
##     so the measured end error is only about 1.6x the measured interior one. The
##     format hides its own worst term.
##
##     None of which is a defect to fix. 1.1 mm is inside the storage quantum, and it
##     is 480x better than the 0.52 m duplication bug `ghost.h` replaced. But the
##     header says reflection "gives the right tangent at the ends" without
##     qualification, and on a curve it does not, so the property is measured
##     **against its own closed form** rather than waved at. The assertion fails if
##     the phantom is ever changed to a duplicate (0.52 m, far too big) or to a true
##     circular tangent (0, far too small).
func _check_circle_against_analytic() -> void:
	var ghost := _synthetic_circle(1441)
	var count := ghost.sample_count()
	var hz := float(ghost.sample_hz())

	var worst_interior := 0.0
	var worst_linear := 0.0
	var worst_ends := 0.0
	var worst_w0 := 0.0
	# Off the knots deliberately: at a knot every interpolator agrees and the
	# measurement would be check 4 again.
	var offsets := [0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875]
	for segment in count - 1:
		var at_an_end := segment == 0 or segment == count - 2
		for offset in offsets:
			var index := float(segment) + float(offset)
			var exact := _circle_at(index / hz)
			var spline: Vector3 = ghost.sample_at_time(index / hz)["position"]
			var spline_error := (spline - exact).length()
			if at_an_end:
				worst_ends = maxf(worst_ends, spline_error)
			else:
				worst_interior = maxf(worst_interior, spline_error)
			# The lerp reference is measured on the interior too: at an end it would be
			# compared against a spline that is answering a different question.
			if not at_an_end:
				worst_linear = maxf(worst_linear, (_linear_at(ghost, index) - exact).length())
	for offset in offsets:
		# `w0(t) = -t(1 - t)^2 / 2`, the weight the phantom point is carried in at.
		# Taken over the offsets actually sampled rather than over its continuous
		# maximum at t = 1/3, because the prediction has to describe this measurement.
		worst_w0 = maxf(worst_w0, absf(-0.5 * offset * (1.0 - offset) * (1.0 - offset)))

	var step_angle := CIRCLE_SPEED / CIRCLE_RADIUS / hz
	var sagitta := CIRCLE_SPEED * CIRCLE_SPEED / CIRCLE_RADIUS / (hz * hz) / 8.0
	# How far `p0 = 2*p1 - p2` lands from the point that was really there. Two chords
	# out along the chord instead of one step round the arc.
	var reflection_miss := 2.0 * CIRCLE_RADIUS * (1.0 - cos(step_angle))
	var predicted_end := worst_w0 * reflection_miss
	var bound := POSITION_QUANTUM * 0.5 * SPLINE_WEIGHT_L1 * sqrt(3.0)

	_ok("Catmull-Rom is at the quantization floor", worst_interior <= bound,
		"%.4f mm against a %.4f mm grid bound" % [worst_interior * 1000.0, bound * 1000.0])
	# Two-sided: too small means the geometry is not what this check thinks it is, too
	# large means the samples are not where they should be.
	_ok("a lerp is short by the predicted sagitta", absf(worst_linear - sagitta) <= bound,
		"%.4f mm measured, %.4f mm predicted (a h^2 / 8)" % [
			worst_linear * 1000.0, sagitta * 1000.0,
		])
	_ok("the spline beats the lerp", worst_linear > worst_interior * 1.8,
		"%.4f mm against %.4f mm, %.2fx" % [
			worst_linear * 1000.0, worst_interior * 1000.0,
			worst_linear / maxf(worst_interior, 1e-12),
		])
	# The end segments against the reflection's own closed form. Not "no worse than the
	# middle", which is what this check said before it was measured and which is false.
	_ok("the end segments are the reflection error",
		absf(worst_ends - predicted_end) <= bound,
		"%.4f mm measured, %.4f mm predicted (|w0| %.4f x %.1f mm miss), %.2fx the interior" % [
			worst_ends * 1000.0, predicted_end * 1000.0, worst_w0, reflection_miss * 1000.0,
			worst_ends / maxf(worst_interior, 1e-12),
		])


## 8. A ghost survives a file, sample for sample.
##
## Bit-identity, not similarity. `ghost.h` quantizes on write precisely so that a
## ghost written on one machine and read on another is the same ghost byte for byte,
## and a round trip that was merely close would mean the recorded stream and the
## stored stream are two different laps that happen to look alike.
func _check_file_round_trip() -> void:
	var ghost := _synthetic_circle(901)
	# A header with everything in it: sectors, hashes, a preset.
	ghost.set_track("test_track")
	ghost.set_track_hash_hex("0xfeedfacecafebeef")
	ghost.set_tuning_hash_hex("0x0123456789abcdef")
	ghost.set_layout(0)
	ghost.set_kart_class(1)
	var durations := PackedFloat64Array([9.5, 11.25, 9.25])
	var closed := ghost.finish_record(30.0, durations)

	var path := SCRATCH_DIR + "/round_trip.ghost"
	var saved := ghost.save(path) if closed else FAILED
	var reloaded := KartGhost.new()
	var report := reloaded.load(path)

	var problems: Array[String] = []
	if not closed:
		problems.append("finish_record refused a valid lap")
	if saved != OK:
		problems.append("save returned %d" % saved)
	if not bool(report.get("ok", false)):
		problems.append("load: %s" % String(report.get("detail", "")))
	if reloaded.sample_count() != ghost.sample_count():
		problems.append("%d samples became %d" % [ghost.sample_count(), reloaded.sample_count()])
	else:
		for i in ghost.sample_count():
			if not _samples_identical(ghost.sample_at_index(i), reloaded.sample_at_index(i)):
				problems.append("sample %d differs" % i)
				break
	# The header too, field by field, because a stream that survived under a header
	# that did not is a lap with the wrong time on it.
	var before := ghost.header()
	var after := reloaded.header()
	for key in ["track", "track_hash", "layout", "kart_class", "sample_hz", "sample_count",
			"lap_time", "sector_count", "tuning_hash", "id"]:
		if before[key] != after[key]:
			problems.append("%s: %s became %s" % [key, before[key], after[key]])
	if reloaded.sector_splits() != ghost.sector_splits():
		problems.append("the splits moved")
	# The cumulative form is what is stored; the durations are the subtraction back.
	var returned := reloaded.sector_durations()
	for i in durations.size():
		if absf(returned[i] - durations[i]) > 5e-7:
			problems.append("sector %d: %f became %f" % [i, durations[i], returned[i]])
	# Written twice, byte-identical. `tuning.h`'s family property, and it is what makes
	# a ghost diffable in a bug report.
	var second := SCRATCH_DIR + "/round_trip_again.ghost"
	ghost.save(second)
	var a := FileAccess.get_file_as_bytes(path)
	if a != FileAccess.get_file_as_bytes(second):
		problems.append("two saves of one ghost differ")
	# And the file the *reloaded* ghost writes is the file it was read from.
	#
	# This is the assertion that actually carries "bit-identical", and the
	# sample-by-sample comparison above does not. `Vector3` is float32, so two distinct
	# millimeter codes could in principle read back as one number through
	# `sample_at_index` — not at these magnitudes, where the engine's grain is 65 times
	# finer than the storage quantum, but the comparison is not *proof* of bit-identity
	# and this is. Load, save, compare every byte including the header.
	var rewritten := SCRATCH_DIR + "/round_trip_reloaded.ghost"
	reloaded.save(rewritten)
	if a != FileAccess.get_file_as_bytes(rewritten):
		problems.append("a reloaded ghost does not write back the bytes it was read from")

	_ok("a ghost round-trips bit-identically", problems.is_empty(),
		"%d samples, %d bytes" % [reloaded.sample_count(), a.size()] if problems.is_empty()
			else "; ".join(problems))


## 9. What a lap costs, against `ghost.h`'s own figure.
##
## `ghost.h` quotes "a 48 s lap ... is 1,440 samples and 25,920 bytes". Its own
## `ghost_samples_for_lap(48.0)` returns 1,441 — one sample at t = 0 and one at every
## interval after it — so the byte figure in the prose is one sample light. Measured
## here from a real recording rather than from either claim, and both are printed.
func _check_bytes_per_lap() -> void:
	var lap := 48.0
	var ticks := int(round(lap * PHYSICS_HZ))
	var ghost := _synthetic_line(ticks + 1)
	ghost.set_track("test_track")
	ghost.finish_record(lap, PackedFloat64Array([16.0, 16.0, 16.0]))
	var path := SCRATCH_DIR + "/cost.ghost"
	var saved := ghost.save(path)
	var on_disk := _file_size(path)
	var body := ghost.sample_count() * KartGhost.SAMPLE_BYTES
	var predicted := KartGhost.bytes_for_lap(lap)

	var problems: Array[String] = []
	if saved != OK:
		problems.append("save returned %d" % saved)
	if body != predicted:
		problems.append("recorded %d body bytes, bytes_for_lap says %d" % [body, predicted])
	if ghost.byte_size() != on_disk:
		problems.append("byte_size says %d, the file is %d" % [ghost.byte_size(), on_disk])
	# The prose figure, and the assertion is that the truth is within one sample of it
	# rather than equal to it — the disagreement is the off-by-one above, and it is
	# worth printing rather than hiding behind a loose bound.
	if absi(body - 25920) > KartGhost.SAMPLE_BYTES:
		problems.append("%d body bytes is more than one sample from ghost.h's 25,920" % body)
	_ok("a 48 s lap costs what ghost.h says", problems.is_empty(),
		"%d samples, %d body + %d header = %d on disk; the prose says 25,920" % [
			ghost.sample_count(), body, on_disk - body, on_disk,
		] if problems.is_empty() else "; ".join(problems))


## 10. An invalid lap is not saved, and neither is an unfinished one.
##
## Four doors into a file and all four have to be shut, because a best lap set with
## two wheels on the grass that got saved anyway is a ghost a driver would chase for
## an evening. The positive control at the end matters as much: a `save` that refused
## everything would pass the first four.
func _check_invalid_lap_is_not_saved() -> void:
	var problems: Array[String] = []
	var path := SCRATCH_DIR + "/should_not_exist.ghost"

	# A recording that was never closed.
	var open_ghost := _synthetic_line(121)
	open_ghost.set_track("test_track")
	if open_ghost.save(path) == OK or FileAccess.file_exists(path):
		problems.append("an unclosed recording was written")

	# A lap time that is not a lap time.
	var zero := _synthetic_line(121)
	zero.set_track("test_track")
	if zero.finish_record(0.0, PackedFloat64Array()):
		problems.append("finish_record accepted a zero lap time")
	if zero.save(path) == OK or FileAccess.file_exists(path):
		problems.append("a ghost with no lap time was written")

	# A recording with no samples at all.
	var empty := KartGhost.new()
	empty.set_track("test_track")
	empty.begin_record()
	if empty.finish_record(48.0, PackedFloat64Array()):
		problems.append("finish_record accepted an empty recording")

	# The real path: a lap `KartLapTimer` says was invalid. Driven through the timer
	# rather than faked, so what is tested is the seam and not a mock of it.
	var timer := KartLapTimer.new()
	timer.begin_even(400.0, 3, 1.0 / PHYSICS_HZ)
	var distance := 0.0
	var completed := false
	for tick in 4000:
		distance += 20.0 / PHYSICS_HZ
		# Off track for one tick in the middle of the lap. `lap_timing.h`'s FIA rule:
		# all four wheels outside the line.
		if timer.advance(fmod(distance, 400.0), tick == 500):
			completed = true
			break
	var invalid := _synthetic_line(1201)
	invalid.set_track("test_track")
	var report := invalid.adopt_lap(timer)
	if not completed:
		problems.append("the timer never completed a lap")
	elif timer.last_was_valid():
		problems.append("the timer called an off-track lap valid")
	elif bool(report.get("ok", true)):
		problems.append("adopt_lap accepted an invalid lap")
	elif invalid.save(path) == OK or FileAccess.file_exists(path):
		problems.append("an invalid lap was written")

	# The positive control, in case the above passes because nothing works at all.
	#
	# **Two laps, not one.** `lap_timing.h` has a `LapInvalidReason::OutLap` — "the
	# first lap of a session, which starts from a standing kart" — so the first lap a
	# session completes is invalid by rule and a control that drove one lap would be
	# asserting that a valid lap saves while handing over an invalid one. This is also
	# the shape a Practice session really has: the ghost is set on a flying lap, which
	# is why `ghost.h` argues about the endpoint tangents at racing speed at all.
	var clean := KartLapTimer.new()
	clean.begin_even(400.0, 3, 1.0 / PHYSICS_HZ)
	var travelled := 0.0
	for tick in 8000:
		travelled += 20.0 / PHYSICS_HZ
		if clean.advance(fmod(travelled, 400.0), false) and clean.laps_completed() >= 2:
			break
	var valid := _synthetic_line(2401)
	valid.set_track("test_track")
	var accepted := valid.adopt_lap(clean)
	if not clean.last_was_valid():
		problems.append("the clean lap was not called valid")
	elif not bool(accepted.get("ok", false)):
		problems.append("adopt_lap refused a clean lap: %s" % String(accepted.get("reason", "")))
	elif valid.save(SCRATCH_DIR + "/valid.ghost") != OK:
		problems.append("a clean lap would not save")
	# And the splits came across as cumulative with the lap time at the end, which is
	# `GhostHeader::is_valid`'s own rule and the one thing the two conventions could
	# have got wrong silently.
	var splits := valid.sector_splits()
	if splits.size() == 3 and absf(splits[2] - clean.last_time()) > 5e-7:
		problems.append("the last split is %f against a %f lap" % [splits[2], clean.last_time()])

	_ok("an invalid lap is never written", problems.is_empty(),
		"4 refusals, 1 acceptance at %.4f s in %d sectors" % [
			clean.last_time(), splits.size(),
		] if problems.is_empty() else "; ".join(problems))


## 11. A header that disagrees with its body is refused.
##
## Both directions, because they fail differently and both look plausible. A body
## short of its header plays a ghost that stops in the middle of the circuit; a body
## longer than its header silently drops the end of the lap. Neither may load, and
## neither may leave a drawable ghost behind in the object that tried.
func _check_header_body_disagreement() -> void:
	var ghost := _synthetic_line(481)
	ghost.set_track("test_track")
	ghost.finish_record(4.0, PackedFloat64Array())
	var path := SCRATCH_DIR + "/truncated.ghost"
	ghost.save(path)
	var whole := FileAccess.get_file_as_bytes(path)

	var problems: Array[String] = []
	var cases := {
		"one sample short": whole.slice(0, whole.size() - KartGhost.SAMPLE_BYTES),
		"one byte short": whole.slice(0, whole.size() - 1),
		"one sample long": whole + whole.slice(0, KartGhost.SAMPLE_BYTES),
	}
	for case_name in cases:
		var bad := SCRATCH_DIR + "/bad.ghost"
		_write_bytes(bad, cases[case_name])
		var reader := KartGhost.new()
		if bool(reader.load(bad).get("ok", false)):
			problems.append("%s loaded" % case_name)
		elif reader.is_complete():
			problems.append("%s left a drawable ghost behind" % case_name)
		DirAccess.remove_absolute(bad)

	# And a file with no body marker at all, which is what any other file on disk looks
	# like to this reader.
	var junk := SCRATCH_DIR + "/junk.ghost"
	_write_bytes(junk, "this is not a ghost\n".to_ascii_buffer())
	if bool(KartGhost.new().load(junk).get("ok", false)):
		problems.append("a file with no body marker loaded")

	# Positive control: the untouched file still loads, so the refusals above are about
	# the damage and not about the reader.
	if not bool(KartGhost.new().load(path).get("ok", false)):
		problems.append("the undamaged file stopped loading")

	_ok("a body that disagrees is refused", problems.is_empty(),
		"3 damaged, 1 junk, 1 intact" if problems.is_empty() else "; ".join(problems))


## 12. A future format is refused; an older one would be migrated.
##
## The ADR-0041 / ADR-0042 split, from the file side. ADR-0042 says a save always
## loads and `ghost.h` follows it: an older version migrates forward, a newer one
## cannot be migrated because that is guessing, and a ghost drawn from a stream this
## build does not understand is a kart driving through the scenery. v1 is the only
## version, so the older half is the identity function and there is nothing on disk to
## test it against yet — ADR-0042's own rule is that the corpus grows when v2 arrives,
## and `tests/data/saves/` is where it goes.
func _check_future_format_refused() -> void:
	var ghost := _synthetic_line(241)
	ghost.set_track("test_track")
	ghost.finish_record(2.0, PackedFloat64Array())
	var path := SCRATCH_DIR + "/future.ghost"
	ghost.save(path)
	var bytes := FileAccess.get_file_as_bytes(path)

	var problems: Array[String] = []
	var at := _find_bytes(bytes, "format 1\n".to_ascii_buffer())
	if at < 0:
		problems.append("the header has no 'format 1' line")
	else:
		# One byte changed and the length identical, so a refusal can only be about the
		# version.
		bytes[at + 7] = "2".to_ascii_buffer()[0]
		var bad := SCRATCH_DIR + "/future_bad.ghost"
		_write_bytes(bad, bytes)
		var reader := KartGhost.new()
		var report := reader.load(bad)
		if bool(report.get("ok", false)):
			problems.append("a format 2 ghost loaded")
		if int(report.get("error", 0)) != ERR_FILE_UNRECOGNIZED:
			problems.append("refused with error %d, not ERR_FILE_UNRECOGNIZED" % int(
				report.get("error", 0)))
		if reader.is_complete():
			problems.append("a refused ghost was left drawable")
	_ok("a newer format is refused, not guessed", problems.is_empty(),
		"format %d is the ceiling" % KartGhost.FORMAT_VERSION if problems.is_empty()
			else "; ".join(problems))


## 13. The id names the slot, and nothing else.
##
## ADR-0042 keys a `ProfileBest` on (track, layout, class) and holds one ghost id per
## key. If an id carried anything else — a timestamp, a lap time, a counter — then
## going faster would write a new file and orphan the old one on every improvement,
## and a profile would accumulate a lap's worth of bytes per personal best forever.
## Minting from the slot makes replacement idempotent, which is what makes "does not
## leak old ones" a property rather than a chore.
##
## The last group is about it being a **filename**.
## `SessionConfig::set_track_id` validates length and nothing else, so a track id
## carrying a `/` or a `..` reaches this function unchallenged.
func _check_id_scheme() -> void:
	var problems: Array[String] = []

	var a := KartGhost.mint_id("test_track", 0, 1)
	if a != KartGhost.mint_id("test_track", 0, 1):
		problems.append("the same slot minted two ids")
	if a == KartGhost.mint_id("test_track", 1, 1):
		problems.append("two layouts share an id")
	if a == KartGhost.mint_id("test_track", 0, 0):
		problems.append("two classes share an id")
	if a == KartGhost.mint_id("other_track", 0, 1):
		problems.append("two tracks share an id")
	# The case the hash is there for: two circuits agreeing on the first fifteen
	# characters. Without the disambiguator they would share a file, and setting a best
	# at one would leave the other's profile entry pointing at a header for the wrong
	# circuit.
	var long_a := KartGhost.mint_id("south_garda_kart_lonato", 0, 1)
	var long_b := KartGhost.mint_id("south_garda_kart_castelletto", 0, 1)
	if long_a == long_b:
		problems.append("two long track ids collided")
	for id in [a, long_a, long_b]:
		if not KartGhost.is_valid_id(id):
			problems.append("'%s' is not a valid id" % id)
		if id.length() > 31:
			problems.append("'%s' is %d characters, past PROFILE_SLUG_CHARS" % [id, id.length()])

	# The path-shaped ones. Each must come back as a slug, and the path built from it
	# must stay inside the ghost directory.
	for hostile in ["../../etc/passwd", "a/b", "a.b", "-leading-dash", "UPPER_CASE"]:
		var minted := KartGhost.mint_id(hostile, 0, 1)
		if minted.is_empty():
			problems.append("'%s' minted nothing" % hostile)
			continue
		if not KartGhost.is_valid_id(minted):
			problems.append("'%s' minted '%s', which is not a slug" % [hostile, minted])
		if not KartGhost.path_for_id(minted).begins_with(KartGhost.ghost_directory() + "/"):
			problems.append("'%s' escaped to %s" % [hostile, KartGhost.path_for_id(minted)])
	if not KartGhost.path_for_id("../evil").is_empty():
		problems.append("path_for_id built a path out of '../evil'")
	if not KartGhost.mint_id("", 0, 1).is_empty():
		problems.append("an empty track minted an id")

	# And the id a ghost reports is the id for its own slot.
	var ghost := KartGhost.new()
	ghost.set_track("test_track")
	ghost.set_layout(0)
	ghost.set_kart_class(1)
	if ghost.id() != a:
		problems.append("a ghost reports '%s' for the slot that mints '%s'" % [ghost.id(), a])
	if KartGhost.path_for_id(a) != KartGhost.ghost_directory() + "/" + a + ".ghost":
		problems.append("path_for_id is not <dir>/<id>.ghost")

	_ok("an id names its slot and stays a filename", problems.is_empty(),
		"%s, %d chars" % [a, a.length()] if problems.is_empty() else "; ".join(problems))


## 14. Going faster replaces one file, and an orphan is reported rather than deleted.
##
## The two halves of the question ADR-0042 leaves open. The first is settled by the
## id: a second, faster lap on the same slot lands on the same path, so the directory
## holds one file per slot however many personal bests are set. The second is a policy,
## and this is the one place it is written down as a test — nothing in `KartGhost`
## removes a file, because an orphan arises from a deleted profile entry or a renamed
## circuit, and in every one of those cases the file is the only copy of a lap somebody
## drove. ADR-0042's rule for a file it cannot use is "moved aside, not overwritten".
func _check_one_file_per_slot() -> void:
	var problems: Array[String] = []
	# A real `user://ghosts/` may hold a driver's laps, so the naming half runs in the
	# scratch directory and only the reporting half touches the real one.
	var kept := KartGhost.new()
	kept.set_track("orphan_probe_a")
	kept.set_layout(0)
	kept.set_kart_class(1)
	_record_line_into(kept, 241)
	kept.finish_record(2.0, PackedFloat64Array())
	var path := SCRATCH_DIR + "/" + kept.id() + ".ghost"
	kept.save(path)
	var first_size := _file_size(path)

	# The same slot, a different lap, which is the shape of a new personal best: same
	# id, same path, one file.
	var faster := KartGhost.new()
	faster.set_track("orphan_probe_a")
	faster.set_layout(0)
	faster.set_kart_class(1)
	_record_line_into(faster, 361)
	faster.finish_record(3.0, PackedFloat64Array())
	if faster.id() != kept.id():
		problems.append("a second lap on one slot minted a different id")
	faster.save(SCRATCH_DIR + "/" + faster.id() + ".ghost")
	var second_size := _file_size(path)
	if second_size == first_size:
		problems.append("the file did not change")

	var dir := DirAccess.open(SCRATCH_DIR)
	var matching := 0
	for file in dir.get_files():
		if file.begins_with("orphan_probe"):
			matching += 1
		if file.ends_with(".tmp"):
			problems.append("a .tmp survived a save: %s" % file)
	if matching != 1:
		problems.append("%d files for one slot" % matching)

	# `orphan_ids` reports and does not delete. Run against the real directory, which
	# may legitimately be empty; what is asserted is that the call leaves the disk
	# alone and that the two extremes answer correctly.
	var before := KartGhost.stored_ids()
	var orphans := KartGhost.orphan_ids(PackedStringArray())
	if before != KartGhost.stored_ids():
		problems.append("orphan_ids changed what is on disk")
	if orphans.size() != before.size():
		problems.append("keeping nothing reported %d of %d as orphans" % [
			orphans.size(), before.size(),
		])
	if KartGhost.orphan_ids(before).size() != 0:
		problems.append("keeping everything still reported orphans")

	_ok("one file per slot, orphans reported", problems.is_empty(),
		"%d file at %d then %d bytes, %d ids in %s" % [
			matching, first_size, second_size, before.size(), KartGhost.ghost_directory(),
		] if problems.is_empty() else "; ".join(problems))


## 15. Comparability is a verdict, not a bool.
##
## `ghost.h`'s central asymmetry against the replay: a ghost is **deliberately** raced
## against a session that differs from the one that set it, so there is no
## `config_hash` and a changed track file warns where a replay refuses. Three verdicts
## and each has to be reachable, or the caveat case collapses into one of the other two
## and a driver is either shown a meaningless ghost or shown none.
func _check_comparability() -> void:
	var ghost := _synthetic_line(241)
	ghost.set_track("test_track")
	ghost.set_track_hash_hex("0xaaaa")
	ghost.set_tuning_hash_hex("0xbbbb")
	ghost.set_layout(0)
	ghost.set_kart_class(1)
	ghost.finish_record(2.0, PackedFloat64Array())

	var session := KartSession.new()
	session.set_track("test_track")
	session.set_track_hash_hex("0xaaaa")
	session.set_layout(KartSession.LAYOUT_FORWARD)
	session.set_kart_class(KartSession.CLASS_KZ2)

	var problems: Array[String] = []
	# A fresh session's tuning hash is the default set's, which is not `0xbbbb`, so this
	# ghost is comparable-with-a-caveat and the caveat is the preset. That is the point:
	# a lap set on a preset is labeled, not rejected.
	var warned := ghost.compare_to_session(session)
	if int(warned["verdict"]) != KartGhost.VERDICT_WARNED:
		problems.append("a different preset gave '%s'" % String(warned["verdict_name"]))
	if not bool(warned["drawable"]) or not bool(warned["tuning_differs"]):
		problems.append("the preset caveat was not reported")

	# A matching tuning hash is the clean case.
	ghost.set_tuning_hash_hex(_session_tuning_hash(session))
	var clean := ghost.compare_to_session(session)
	if int(clean["verdict"]) != KartGhost.VERDICT_COMPARABLE:
		problems.append("an identical slot and preset gave '%s'" % String(clean["verdict_name"]))

	# A changed track file warns rather than refusing, and stays drawable.
	session.set_track_hash_hex("0xcccc")
	var changed := ghost.compare_to_session(session)
	if int(changed["verdict"]) != KartGhost.VERDICT_WARNED or not bool(changed["drawable"]):
		problems.append("a changed track file gave '%s'" % String(changed["verdict_name"]))
	if not bool(changed["track_changed"]):
		problems.append("the changed track was not reported")
	session.set_track_hash_hex("0xaaaa")

	# Another track, layout or class is meaningless and must not be drawn.
	session.set_track("other_track")
	var elsewhere := ghost.compare_to_session(session)
	if int(elsewhere["verdict"]) != KartGhost.VERDICT_INCOMPARABLE or bool(elsewhere["drawable"]):
		problems.append("another track gave '%s'" % String(elsewhere["verdict_name"]))
	session.set_track("test_track")
	session.set_layout(KartSession.LAYOUT_REVERSE)
	if int(ghost.compare_to_session(session)["verdict"]) != KartGhost.VERDICT_INCOMPARABLE:
		problems.append("another layout was called comparable")
	session.set_layout(KartSession.LAYOUT_FORWARD)
	session.set_kart_class(KartSession.CLASS_OK)
	if int(ghost.compare_to_session(session)["verdict"]) != KartGhost.VERDICT_INCOMPARABLE:
		problems.append("another class was called comparable")

	_ok("comparability is comparable, warned, or not", problems.is_empty(),
		"3 verdicts, 3 refusals" if problems.is_empty() else "; ".join(problems))


# --- the truths -----------------------------------------------------------------


## The exact circle: `CIRCLE_RADIUS` in the XZ plane at `CIRCLE_SPEED`, starting on
## the +X axis and turning toward -Z. Godot's forward is -Z, so this is a left-hand
## corner taken at the §6.4 speed for a 30 m radius.
func _circle_at(time_s: float) -> Vector3:
	var angle := CIRCLE_SPEED / CIRCLE_RADIUS * time_s
	return Vector3(CIRCLE_RADIUS * cos(angle), 0.0, -CIRCLE_RADIUS * sin(angle))


## The exact straight line: down -Z at `LINE_SPEED`, which is 0.700 m per sample and
## therefore exactly on the storage grid.
func _line_at(time_s: float) -> Vector3:
	return Vector3(4.0, 0.25, -LINE_SPEED * time_s)


## A recorded circle, fed one tick at a time at the physics rate so the recorder's own
## stride is what produces the samples.
##
## The kart faces along the tangent, which is what a kart on a circle is doing, so the
## yaw stream sweeps past a full turn and crosses the -pi/+pi seam — the one place
## `ghost.h`'s angle wrap can be wrong and look right.
func _synthetic_circle(ticks: int) -> KartGhost:
	var ghost := KartGhost.new()
	ghost.set_track("circle")
	ghost.begin_record()
	for tick in ticks:
		var time := float(tick) / PHYSICS_HZ
		var yaw := CIRCLE_SPEED / CIRCLE_RADIUS * time
		ghost.record_tick(Transform3D(
			Basis.from_euler(Vector3(0.0, yaw, 0.0), EULER_ORDER_YXZ), _circle_at(time)))
	return ghost


func _synthetic_line(ticks: int) -> KartGhost:
	var ghost := KartGhost.new()
	ghost.set_track("line")
	_record_line_into(ghost, ticks)
	return ghost


func _record_line_into(ghost: KartGhost, ticks: int) -> int:
	ghost.begin_record()
	for tick in ticks:
		ghost.record_tick(Transform3D(Basis(), _line_at(float(tick) / PHYSICS_HZ)))
	return ghost.sample_count()


## Linear interpolation over the stored samples — the reference `ghost.h`'s rate
## argument is made against.
##
## A second interpolator, deliberately, and it is not a shipped path: the only way to
## state that the format drives a Catmull-Rom rather than a lerp is to run the lerp
## over the same samples and measure the difference. `ghost.h` puts the two at 1.85 mm
## and 0.005 mm on this exact geometry.
func _linear_at(ghost: KartGhost, index: float) -> Vector3:
	var i := clampi(int(floor(index)), 0, ghost.sample_count() - 2)
	var t := index - float(i)
	var a: Vector3 = ghost.sample_at_index(i)["position"]
	var b: Vector3 = ghost.sample_at_index(i + 1)["position"]
	return a + (b - a) * t


# --- plumbing -------------------------------------------------------------------


## Shortest arc between two angles, so a comparison across the -pi/+pi seam does not
## read as a whole turn of error. `ghost.h`'s `ghost_angle_delta` in GDScript.
func _arc(from: float, to: float) -> float:
	return wrapf(to - from, -PI, PI)


## A number in scientific notation, for the figures here that are 1e-16 and have to be
## legible as such.
##
## **`%` has no `%e`** — CLAUDE.md records it, and it does not error: it silently
## leaves the literal in the string, so every substitution in that format string is
## lost too. It cost a run of this file, which printed four rows reading
## `%.3e m, %.3e rad over %d samples` over a table that was otherwise correct. Anything
## smaller than the storage quantum goes through here.
func _sci(value: float) -> String:
	return String.num_scientific(value)


## Two samples are the same sample when all four stored quantities are identical. `==`
## on floats, not a tolerance: the stream is quantized, so bit-identity is the claim
## and anything looser would pass a round trip that had lost a code.
func _samples_identical(a: Dictionary, b: Dictionary) -> bool:
	if a.is_empty() or b.is_empty():
		return false
	var pa: Vector3 = a["position"]
	var pb: Vector3 = b["position"]
	return pa == pb and float(a["yaw"]) == float(b["yaw"]) \
			and float(a["pitch"]) == float(b["pitch"]) \
			and float(a["roll"]) == float(b["roll"])


## A session's tuning hash, read back through the one thing that copies it.
## `KartSession` does not expose one — the tuning rides inside `config_hash` — so a
## throwaway ghost adopts the session and reports the field.
func _session_tuning_hash(session: KartSession) -> String:
	var probe := KartGhost.new()
	probe.adopt_session(session)
	return probe.get_tuning_hash_hex()


## The offset of `needle` in `haystack`, or -1. Searched as bytes rather than through
## `get_string_from_ascii`, which stops at the first null and a ghost's body is full of
## them.
func _find_bytes(haystack: PackedByteArray, needle: PackedByteArray) -> int:
	for start in haystack.size() - needle.size() + 1:
		var hit := true
		for i in needle.size():
			if haystack[start + i] != needle[i]:
				hit = false
				break
		if hit:
			return start
	return -1


func _write_bytes(path: String, bytes: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer(bytes)
	file.close()


func _file_size(path: String) -> int:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return -1
	var size := int(file.get_length())
	file.close()
	return size


## One result line, `tuning_probe.gd`'s format.
func _ok(name: String, condition: bool, detail: String = "") -> bool:
	if condition:
		_passed += 1
		_lines.append("check %-38s PASS%s" % [
			name, "" if detail.is_empty() else "   " + detail,
		])
	else:
		_failed += 1
		_lines.append("check %-38s FAIL   %s" % [name, detail])
	return condition


func _report_and_quit() -> void:
	_lines.append("")
	_lines.append("checks %d passed %d failed" % [_passed, _failed])
	print("\n".join(_lines))
	if _failed > 0:
		printerr("the ghost format does not survive contact with the engine")
	_clean_scratch()
	_done = true
	quit(1 if _failed > 0 else 0)


## Scratch files, before and after. Removed rather than overwritten, for
## `tuning_probe.gd`'s reason: a leftover file from a previous run passing for this
## run's output is the `SKIP_IMPORT=1` trap in another costume.
func _clean_scratch() -> void:
	var dir := DirAccess.open(SCRATCH_DIR)
	if dir != null:
		for file in dir.get_files():
			DirAccess.remove_absolute(SCRATCH_DIR + "/" + file)
	DirAccess.remove_absolute(SCRATCH_DIR)
	DirAccess.make_dir_recursive_absolute(SCRATCH_DIR)
