extends SceneTree

## Headless M0 acceptance check. Exits non-zero on failure.
##
##     godot --headless --path . --script tools/verify/verify_extension.gd
##
## This is what CI runs and what "M0 is done" means concretely. It asserts the
## things that break silently: an extension that failed to load, a binary built
## against a different API than the engine running it, a float width mismatch
## across the boundary, a physics tick that drifted off 120 Hz, and an input map
## missing an action the vehicle code will assume exists.
##
## Deliberately not a unit-test framework. C++ unit tests link against src/core/
## without an engine at all (docs/DECISIONS.md ADR-0017); this checks the
## integration those tests cannot see.

const REQUIRED_ACTIONS: Array[StringName] = [
	&"throttle",
	&"brake",
	&"steer_left",
	&"steer_right",
	&"shift_up",
	&"shift_down",
	&"clutch",
	&"look_back",
	&"respawn",
	&"camera_cycle",
	&"pause",
]

const REQUIRED_PHYSICS_HZ := 120

var _failures: PackedStringArray = []


func _initialize() -> void:
	_check_extension_loaded()
	_check_api_version_matches_engine()
	_check_core_is_reachable()
	_check_kz_reference_is_self_consistent()
	_check_physics_tick_rate()
	_check_input_map()
	_check_renderer()

	if _failures.is_empty():
		print("M0 verification passed.")
		quit(0)
		return

	printerr("M0 verification failed:")
	for failure: String in _failures:
		printerr("  - " + failure)
	quit(1)


func _fail(message: String) -> void:
	_failures.append(message)


func _check_extension_loaded() -> void:
	if not ClassDB.class_exists("KartCore"):
		_fail("KartCore is not registered — the GDExtension did not load. Run: scons target=editor")
		return

	# Registered abstract on purpose. If this ever becomes instantiable it means
	# the registration macro changed, and static-only access is no longer enforced.
	if ClassDB.can_instantiate("KartCore"):
		_fail("KartCore is instantiable — it should be registered with GDREGISTER_ABSTRACT_CLASS")


func _check_api_version_matches_engine() -> void:
	if not ClassDB.class_exists("KartCore"):
		return

	var info: Dictionary = KartCore.build_info()
	var engine := Engine.get_version_info()
	var built_against: String = info["godot_api_version"]
	var running := "%d.%d" % [engine["major"], engine["minor"]]

	# Compared at minor granularity. GDExtension is stable across patch releases
	# but not across minors, and 4.7.0 bindings in a 4.8 editor is exactly the
	# failure this catches before it turns into a crash somewhere unrelated.
	if not built_against.begins_with(running + "."):
		_fail(
			"API mismatch: extension built against Godot %s, running on Godot %s"
			% [built_against, engine["string"]]
		)

	# Godot's default build is single precision. A double-precision extension
	# against a single-precision engine mis-marshals every float that crosses the
	# boundary, and it does so quietly.
	if info["float_precision"] != "single":
		_fail("Extension float precision is %s; the engine build is single" % info["float_precision"])


func _check_core_is_reachable() -> void:
	if not ClassDB.class_exists("KartCore"):
		return

	# Exact, not approximate: 100 / 3.6 is representable closely enough that a
	# tolerance here would hide a unit bug rather than absorb float noise.
	var ms: float = KartCore.kmh_to_ms(100.0)
	if not is_equal_approx(ms, 100.0 / 3.6):
		_fail("kmh_to_ms(100.0) returned %f, expected %f" % [ms, 100.0 / 3.6])

	var round_trip: float = KartCore.ms_to_kmh(KartCore.kmh_to_ms(140.0))
	if not is_equal_approx(round_trip, 140.0):
		_fail("km/h round trip lost precision: 140.0 -> %f" % round_trip)

	var rads: float = KartCore.rpm_to_rads(13000.0)
	if not is_equal_approx(KartCore.rads_to_rpm(rads), 13000.0):
		_fail("rpm round trip lost precision at 13000 rpm")


func _check_kz_reference_is_self_consistent() -> void:
	if not ClassDB.class_exists("KartCore"):
		return

	var kz: Dictionary = KartCore.kz_reference()

	# Each min must be below its max. Cheap, but these constants are the physics
	# validation targets in M3b — a transposed pair would make the suite assert
	# against an empty range and pass by accident.
	var ranges := {
		"top_speed": ["top_speed_min_kmh", "top_speed_max_kmh"],
		"zero_to_100": ["zero_to_100_kmh_min_s", "zero_to_100_kmh_max_s"],
		"lateral_g": ["lateral_g_min", "lateral_g_max"],
		"braking_g": ["braking_g_min", "braking_g_max"],
		"powerband": ["powerband_min_rpm", "powerband_max_rpm"],
	}

	for label: String in ranges:
		var keys: Array = ranges[label]
		if kz[keys[0]] >= kz[keys[1]]:
			_fail("KZ reference range %s is inverted: %s >= %s" % [label, kz[keys[0]], kz[keys[1]]])

	if kz["mass_with_driver_kg"] != 175.0:
		_fail("KZ minimum mass is %s kg; the CIK-FIA class minimum is 175" % kz["mass_with_driver_kg"])

	if kz["gear_count"] != 6:
		_fail("KZ gear count is %s; the class is 6-speed" % kz["gear_count"])

	var peak: float = kz["peak_power_rpm"]
	if peak < kz["powerband_min_rpm"] or peak > kz["powerband_max_rpm"]:
		_fail("Peak power rpm %s falls outside the usable powerband" % peak)


func _check_physics_tick_rate() -> void:
	if Engine.physics_ticks_per_second != REQUIRED_PHYSICS_HZ:
		_fail(
			"Physics tick is %d Hz, expected %d — tire models go unstable below this (ARCHITECTURE.md §6)"
			% [Engine.physics_ticks_per_second, REQUIRED_PHYSICS_HZ]
		)


func _check_input_map() -> void:
	for action: StringName in REQUIRED_ACTIONS:
		if not InputMap.has_action(action):
			_fail("Input action '%s' is missing. Run tools/setup/generate_input_map.gd" % action)
			continue
		if InputMap.action_get_events(action).is_empty():
			_fail("Input action '%s' has no events bound" % action)


func _check_renderer() -> void:
	var method: String = ProjectSettings.get_setting("rendering/renderer/rendering_method", "")
	if method != "forward_plus":
		_fail("Renderer is '%s', expected 'forward_plus' (ARCHITECTURE.md §4)" % method)
