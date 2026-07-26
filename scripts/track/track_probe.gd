extends SceneTree

## Measures the test track, headless, and prints numbers rather than opinions.
##
##     godot --headless --path . --script res://scripts/track/track_probe.gd -- \
##         --case=surfaces
##
##   --case=all        run every case below (the default)
##   --case=surfaces   what each wheel is standing on at six placed points
##   --case=braking    threshold braking on asphalt against the same on grass
##   --case=crossing   a wheel driven across a curb edge at speed
##   --case=lap        an open-loop run, checking nothing falls through anything
##
## ## Why this exists rather than an eyeball on a still
##
## Three of the claims this track has to make are invisible in a render. "The kart
## is standing on the surface you think it is" is a `surface_type` integer coming
## back out of `KartBody::query_ground`, and a curb and a strip of red paint look
## identical from a camera. "Grass costs something" is a deceleration ratio.
## "Nothing falls through anything" is a minimum height over a run. All three are
## numbers, so they are measured here and the stills are left to answer the
## questions stills are good at.
##
## Nothing in here is a gate. `tools/verify/verify.sh`, `tests/run.sh` and
## `tools/verify/drive.sh` are the gates and this scene does not appear in any of
## them; this is the instrument that says whether the geometry is what it claims,
## and it is run by hand when the layout changes.

const SCENE_PATH := "res://scenes/game/test_track.tscn"

## Ticks to let the kart settle after being placed. 120 is one second at the
## project's physics rate — long enough for the suspension to stop ringing and
## short enough that a probe of six placements is instant.
const SETTLE_TICKS := 120

## Entry speed for the braking case, m/s. 20 m/s is 72 km/h: fast enough that the
## asphalt figure is a real threshold-braking number rather than a crawl, and slow
## enough that the same run on grass does not simply leave the measured area.
const BRAKE_ENTRY_MS := 20.0
const BRAKE_TICKS := 90

var _root: Node3D
var _kart: KartBody
var _layout: TrackLayout
var _case := "all"

var _tick := 0
var _stage := 0
var _stage_tick := 0
var _input := {"throttle": 0.0, "brake": 0.0, "steer": 0.0}

## The stage list is built once and walked, so a case that is switched off simply
## is not in it and no stage has to know about any other.
var _stages: PackedStringArray = []
var _placements: Array[Dictionary] = []
var _brake_samples: Array[float] = []
var _asphalt_brake_g := 0.0
var _min_height := INF
var _lap_peak_ms := 0.0
var _lap_surfaces := {}
var _crossing_min_travel := INF
var _crossing_lost_contact := 0


func _initialize() -> void:
	var args := Cmdline.parse()
	_case = Cmdline.as_string(args, "case", "all")

	if not ClassDB.class_exists("KartBody"):
		printerr("KartBody is not registered — build the extension: scons target=editor arch=arm64")
		quit(1)
		return

	var packed: PackedScene = load(SCENE_PATH)
	if packed == null:
		printerr("could not load " + SCENE_PATH)
		quit(1)
		return
	_root = packed.instantiate() as Node3D
	get_root().add_child(_root)


func _physics_process(_delta: float) -> bool:
	# The scene is parented but not yet inside the tree during `_initialize`, so
	# its `_ready` has not run and it has no children to find. Everything exists by
	# the first physics tick, which is where the lookup belongs — `shoot.gd` and
	# `drive_probe.gd` both have this shape for the same reason.
	if _kart == null:
		_kart = _root.find_child("Kart", false, false) as KartBody
		if _kart == null:
			printerr("no Kart in the scene — is assets/generated/kart.glb built?")
			quit(1)
			return true
		_layout = TrackLayout.new()
		_kart.input_driver = func() -> Dictionary: return _input
		_begin()
		return false

	_tick += 1
	_stage_tick += 1
	return _run()


# --- cases -----------------------------------------------------------------


func _begin() -> void:
	print("== test track probe: case %s ==" % _case)
	if _wants("surfaces"):
		_placements = _placement_list()
	_advance()


## Seven places the kart is put down, chosen so that every surface the track
## carries is stood on at least once and so that the two easiest to get wrong — the
## curb, which is 30 mm of geometry, and the road ribbon, which is a surface rather
## than a slab — are each checked more than once.
##
## The curb placements are **not** centered on the curb. A kart's track is 1.4 m
## and `track_ribbon.gd`'s curb is 1.0 m wide, so no axle ever has both wheels on
## one, which is true of a real kart on a real kerb. A kart centered on the curb
## straddles it and touches nothing but road and grass, which is what the first run
## of this probe measured and reported as a missing curb. The offset used instead
## puts the outer pair on the curb and the inner pair on the asphalt — the state a
## driver is actually in when riding an apex.
func _placement_list() -> Array[Dictionary]:
	var half := TrackRibbon.TRACK_WIDTH * 0.5
	# Half the kart's rear track is about 0.7 m; 0.2 m inboard of the asphalt edge
	# puts the outer wheels 0.5 m onto a 1.0 m curb.
	var riding := half - 0.2
	return [
		{"name": "grid, centerline", "distance": 30.0, "offset": 0.0, "expect": "all asphalt"},
		{
			"name": "T1 kink, riding inside curb",
			"distance": _mid(TrackLayout.SEG_KINK), "offset": riding,
			"expect": "right pair curb",
		},
		{
			"name": "T2 hairpin, riding inside curb",
			"distance": _mid(TrackLayout.SEG_HAIRPIN), "offset": -riding,
			"expect": "left pair curb",
		},
		{
			"name": "T4 long corner, road",
			"distance": _mid(TrackLayout.SEG_LONG_CORNER), "offset": 0.0,
			"expect": "all asphalt",
		},
		{
			"name": "T4 long corner, riding curb",
			"distance": _mid(TrackLayout.SEG_LONG_CORNER), "offset": -riding,
			"expect": "left pair curb",
		},
		{
			"name": "T4 long corner, over the curb",
			"distance": _mid(TrackLayout.SEG_LONG_CORNER),
			"offset": -(half + TrackRibbon.CURB_WIDTH + 3.0),
			"expect": "all grass",
		},
		{
			"name": "start straight, run-off",
			"distance": 120.0, "offset": half + 12.0, "expect": "all grass",
		},
	]


func _run() -> bool:
	match _stage_name():
		"surfaces":
			if _stage_tick < SETTLE_TICKS:
				return false
			var placement: Dictionary = _placements[_placement_index()]
			var height := _kart.global_position.y
			print("  %-32s  y %+7.4f m   %s   expected %s" % [
				placement["name"], height, _surfaces(), placement["expect"],
			])
			if height < -0.5:
				printerr("    FELL THROUGH: the kart is %.3f m below the road" % height)
			_advance()
			return false
		"brake_asphalt", "brake_grass":
			return _brake_stage()
		"crossing":
			return _crossing_stage()
		"lap":
			return _lap_stage()
	return true


## Threshold braking from `BRAKE_ENTRY_MS`, on the road and then on the run-off.
##
## Deceleration is differenced from the body's own speed rather than read off
## `get_longitudinal_g()`, so the number does not depend on the same filter the HUD
## reads. The two runs are otherwise identical — same place on the lap, same entry
## speed, same input — so what is left between them is the surface.
func _brake_stage() -> bool:
	var on_grass := _stage_name() == "brake_grass"
	if _stage_tick == 1:
		var offset := TrackRibbon.TRACK_WIDTH * 0.5 + 12.0 if on_grass else 0.0
		_place(140.0, offset)
		_brake_samples.clear()
		return false
	if _stage_tick == 2:
		# Launched rather than driven up to speed: an accelerating run reaches the
		# two surfaces at different speeds and then the braking figures are not
		# comparable. Second gear so the engine braking term is in a normal place.
		_kart.linear_velocity = -_kart.global_transform.basis.z * BRAKE_ENTRY_MS
		_kart.engage(2, BRAKE_ENTRY_MS)
		_input = {"throttle": 0.0, "brake": 1.0, "steer": 0.0}
		return false
	_brake_samples.append(_kart.speed_ms)
	if _stage_tick < BRAKE_TICKS:
		return false

	var rate := float(Engine.physics_ticks_per_second)
	var first: float = _brake_samples[0]
	var last: float = _brake_samples[_brake_samples.size() - 1]
	var seconds := float(_brake_samples.size() - 1) / rate
	var mean_g := (first - last) / seconds / 9.80665
	print("  braking on %-8s  %5.1f -> %5.1f km/h in %.2f s   mean %.3f g   %s" % [
		"grass" if on_grass else "asphalt",
		first * 3.6, last * 3.6, seconds, mean_g, _surfaces(),
	])
	if on_grass:
		# `surface.h` gives grass 0.18 against asphalt's 1.00, and both figures
		# include an engine-braking term that the surface does not scale, so the
		# ratio is expected to be above 0.18 rather than equal to it. What is being
		# asserted here is only that the surface reaches the brakes at all.
		print("  grass / asphalt = %.3f   (surface.h grip ratio is 0.180)" % [
			mean_g / _asphalt_brake_g if _asphalt_brake_g != 0.0 else 0.0,
		])
	else:
		_asphalt_brake_g = mean_g
	_input = {"throttle": 0.0, "brake": 0.0, "steer": 0.0}
	_advance()
	return false


## A wheel driven across a curb edge, sideways, at speed.
##
## Placed on the run-off outboard of the Turn 4 exit curb and pointed across it,
## which is the worst case #139 describes — a wheel crossing a vertical face fast
## enough to pass through it between ticks. This does not settle #139: the ticket
## wants repeated crossings and a cost measurement against §15's budget. What it
## answers is narrower and is the thing this track has to get right, which is
## whether the curb is rideable rather than a wall, and whether the body ends up
## under the road.
func _crossing_stage() -> bool:
	if _stage_tick == 1:
		var distance := _layout.segment_end(TrackLayout.SEG_LONG_CORNER) - 20.0
		_place(distance, TrackRibbon.TRACK_WIDTH * 0.5 + 6.0)
		return false
	if _stage_tick == 2:
		# Aimed across the track rather than along it: the kart is pointed down the
		# lap and given a velocity that is mostly lateral, so it arrives at the curb
		# edge at a high crossing rate without needing a steering input that would
		# also be measuring the steering model.
		var sample := _layout.nearest_sample(
			_layout.segment_end(TrackLayout.SEG_LONG_CORNER) - 20.0)
		var inward := -TrackLayout.right(sample["heading"])
		_kart.linear_velocity = inward * 12.0 + TrackLayout.forward(sample["heading"]) * 8.0
		return false
	_crossing_min_travel = minf(_crossing_min_travel, _kart.global_position.y)
	for wheel: Dictionary in _kart.wheel_report():
		if not bool(wheel["contact"]):
			_crossing_lost_contact += 1
	if _stage_tick < 150:
		return false
	print("  curb crossing at 14.4 m/s (12 m/s lateral)   lowest chassis y %+7.4f m" % [
		_crossing_min_travel,
	])
	print("    wheel-ticks with no contact: %d of %d   final %s" % [
		_crossing_lost_contact, (150 - 2) * 4, _surfaces(),
	])
	if _crossing_min_travel < -0.10:
		printerr("    TUNNELED: the chassis went %.3f m below the road" % _crossing_min_travel)
	_advance()
	return false


## An open-loop run down the start straight and into Turn 1, at full throttle.
##
## Not a lap — an open-loop input cannot drive a hairpin and pretending otherwise
## would be measuring the steering model. The kart therefore runs **off** the track
## at the Turn 1 kink and finishes on the grass, which is deliberate: the one claim
## a still cannot make is that a kart at speed on this geometry stays on top of it,
## and the interesting part of that claim is the transition. The minimum chassis
## height over the whole run and the split of wheel-ticks by surface are the
## report.
func _lap_stage() -> bool:
	if _stage_tick == 1:
		_place(30.0, 0.0)
		_input = {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
		return false
	if _stage_tick < 10:
		return false
	_min_height = minf(_min_height, _kart.global_position.y)
	_lap_peak_ms = maxf(_lap_peak_ms, _kart.speed_ms)
	for wheel: Dictionary in _kart.wheel_report():
		var key := int(wheel["surface"])
		_lap_surfaces[key] = int(_lap_surfaces.get(key, 0)) + 1
	if _stage_tick < 900:
		return false
	print("  900 ticks at full throttle from the grid: peak %.1f km/h, ended at %.1f" % [
		_lap_peak_ms * 3.6, _kart.speed_ms * 3.6,
	])
	print("    lowest chassis y %+7.4f m   wheel-ticks by surface %s" % [
		_min_height, _lap_surfaces,
	])
	if _min_height < -0.05:
		printerr("    FELL THROUGH: %.3f m below the road" % _min_height)
	_advance()
	return false


# --- placement and plumbing ------------------------------------------------


## Put the kart down at a centerline distance and lateral offset.
##
## Dropped from 150 mm rather than set at rest height, for `proving_ground.gd`'s
## reason: placing a kart at exactly wheel height starts the first tick with the
## tires interpenetrating and Jolt resolves that by pushing them apart.
func _place(distance: float, offset: float) -> void:
	var sample := _layout.nearest_sample(distance)
	var position: Vector3 = sample["position"]
	position += TrackLayout.right(sample["heading"]) * offset
	position.y = 0.15
	var basis := Basis(Vector3.UP, -float(sample["heading"]))
	_kart.set_spawn(Transform3D(basis, position))
	_kart.respawn()
	_input = {"throttle": 0.0, "brake": 0.0, "steer": 0.0}


func _surfaces() -> String:
	var parts: Array[String] = []
	for wheel: Dictionary in _kart.wheel_report():
		var names := ["asphalt", "curb", "grass", "dirt"]
		var surface := int(wheel["surface"])
		var label: String = names[surface] if surface < names.size() else "?"
		parts.append("%s=%s" % [String(wheel["name"]), "air" if not bool(wheel["contact"]) else label])
	return " ".join(parts)


func _mid(segment: int) -> float:
	return (_layout.segment_start(segment) + _layout.segment_end(segment)) * 0.5


func _stage_name() -> String:
	return _stages[_stage] if _stage < _stages.size() else ""


func _placement_index() -> int:
	var index := 0
	for position in _stage:
		if _stages[position] == "surfaces":
			index += 1
	return index


func _advance() -> void:
	if _stages.is_empty():
		if _wants("surfaces"):
			for _placement in _placement_list():
				_stages.append("surfaces")
		if _wants("braking"):
			_stages.append("brake_asphalt")
			_stages.append("brake_grass")
		if _wants("crossing"):
			_stages.append("crossing")
		if _wants("lap"):
			_stages.append("lap")
		_stage = -1
	_stage += 1
	_stage_tick = 0
	if _stage >= _stages.size():
		print("== done, %d ticks ==" % _tick)
		quit(0)
		return
	# Stage entry. Only the placement cases need it — the other three place the
	# kart themselves on their first tick, because they also have to set a velocity
	# on the tick after and a single entry hook would have to know that.
	if _stage_name() == "surfaces":
		var placement: Dictionary = _placements[_placement_index()]
		_place(placement["distance"], placement["offset"])


func _wants(name: String) -> bool:
	return _case == "all" or _case == name
