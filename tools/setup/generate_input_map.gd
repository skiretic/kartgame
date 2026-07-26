extends SceneTree

## Writes the default input map into project.godot.
##
##     godot --headless --path . --script tools/setup/generate_input_map.gd
##
## The input map is generated rather than hand-written because a serialized
## InputEvent in project.godot is a long Object(...) literal with two dozen
## fields, and a typo in one produces a project that fails to open rather than an
## action that fails to fire. This file is the readable source; project.godot is
## its output, and both are committed.
##
## Re-running is safe: it replaces the actions it owns and leaves anything else,
## including the engine's built-in ui_* actions, untouched.
##
## Gamepad indices are SDL's, which Godot inherits along with the SDL controller
## database. That is what makes the same mapping correct on a DualSense, an Xbox
## pad, and a generic clone without per-device special cases — see
## ARCHITECTURE.md §9.

const DEADZONE_STICK := 0.15

## Triggers rest at zero and are pressed one way only, so their deadzone only has
## to reject sensor noise. A stick deadzone here would eat real throttle travel.
const DEADZONE_TRIGGER := 0.05

const DEADZONE_BUTTON := 0.5


func _initialize() -> void:
	var actions := _action_definitions()

	for action_name: String in actions:
		var setting := "input/" + action_name
		ProjectSettings.set_setting(setting, actions[action_name])
		# Without this the action exists at runtime but does not appear in the
		# editor's Input Map tab, which makes remapping UI impossible to build.
		ProjectSettings.set_initial_value(setting, {})

	var err := ProjectSettings.save()
	if err != OK:
		push_error("Failed to save project.godot: %d" % err)
		quit(1)
		return

	print("Wrote %d input actions to project.godot" % actions.size())
	quit(0)


func _action_definitions() -> Dictionary:
	var actions := {}

	# --- Driving -------------------------------------------------------------
	# Throttle and brake are analog first. ARCHITECTURE.md §9 forbids gameplay
	# logic from assuming analog triggers exist, so each also has a digital
	# fallback that reports a full-scale value.
	actions["throttle"] = _action(DEADZONE_TRIGGER, [
		_joy_axis(JOY_AXIS_TRIGGER_RIGHT, 1.0),
		_key(KEY_W),
		_key(KEY_UP),
	])

	actions["brake"] = _action(DEADZONE_TRIGGER, [
		_joy_axis(JOY_AXIS_TRIGGER_LEFT, 1.0),
		_key(KEY_S),
		_key(KEY_DOWN),
	])

	# Steering is split into two half-axis actions rather than one signed axis.
	# Godot actions carry unsigned strength, so the sim reads
	# steer_right - steer_left and gets a clean -1..1 with per-side deadzones.
	actions["steer_left"] = _action(DEADZONE_STICK, [
		_joy_axis(JOY_AXIS_LEFT_X, -1.0),
		_key(KEY_A),
		_key(KEY_LEFT),
	])

	actions["steer_right"] = _action(DEADZONE_STICK, [
		_joy_axis(JOY_AXIS_LEFT_X, 1.0),
		_key(KEY_D),
		_key(KEY_RIGHT),
	])

	# --- Drivetrain (ARCHITECTURE.md §6.3) -----------------------------------
	# Shifts are discrete on a gamepad. A real KZ has a hand lever; paddles are the
	# honest gamepad approximation and the assist layer papers over the rest.
	actions["shift_up"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_RIGHT_SHOULDER),
		_key(KEY_E),
	])

	actions["shift_down"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_LEFT_SHOULDER),
		_key(KEY_Q),
	])

	# A DualSense has no third analog axis free, so on a pad the clutch is digital
	# and auto-clutch carries the launch. An analog clutch arrives with pedal sets,
	# which is why the sim reads this as a float rather than a bool.
	actions["clutch"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_X),
		_key(KEY_SHIFT),
	])

	# --- Race ----------------------------------------------------------------
	actions["look_back"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_Y),
		_key(KEY_C),
	])

	actions["respawn"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_B),
		_key(KEY_R),
	])

	actions["camera_cycle"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_BACK),
		_key(KEY_V),
	])

	actions["pause"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_START),
		_key(KEY_ESCAPE),
	])

	# --- Debug ---------------------------------------------------------------
	# Keyboard only. These must never be reachable from a pad during a race.
	actions["debug_telemetry"] = _action(DEADZONE_BUTTON, [_key(KEY_F3)])
	actions["debug_camera"] = _action(DEADZONE_BUTTON, [_key(KEY_F4)])
	actions["debug_physics_draw"] = _action(DEADZONE_BUTTON, [_key(KEY_F5)])

	# --- Tuning --------------------------------------------------------------
	#
	# The overlay from ROADMAP M3b's last bullet, ADR-0037. These are real actions
	# rather than raw keycodes read in a script, which is what the `[` / `]`
	# prototype in `test_track.gd` did — that made two keys invisible to the input
	# map, to the pad bindings, and to the list CLAUDE.md keeps of what every
	# control does. The project has already shipped one milestone where the shift,
	# clutch and look-back keys were documented nowhere and a driver concluded the
	# gearbox was broken.
	#
	# **The d-pad, and only the d-pad, is on the pad.** Every other control on a
	# DualSense is spent: both triggers are throttle and brake, both shoulders are
	# the shifter, and all four face buttons plus Create and Options are bound
	# above. The d-pad is the one surface left, and it is the right one anyway —
	# four discrete directions is exactly a list and a value.
	#
	# The consequence is stated rather than hidden: **`tune_toggle` has no pad
	# binding**, so a driver on a pad reaches over and presses F2 once to open the
	# overlay, and then never touches the keyboard again. A chord would have been
	# the alternative and it would have fired during a race.
	#
	# **The keyboard half is deliberately not the arrow keys, and that was found
	# the hard way.** The arrows are the second binding on `throttle`, `brake`,
	# `steer_left` and `steer_right`, and `KartBody::gather_input` reads those by
	# **polling** `Input.get_action_strength` in `_physics_process` — not through
	# the event queue. `set_input_as_handled()` does not touch the `Input`
	# singleton, so an overlay that consumed the arrows would still have applied
	# full right lock every time somebody held Right to sweep a value. The overlay
	# would have looked correct and the kart would have driven into the grass.
	#
	# So: PageUp and PageDown move the selection, and `[` and `]` move the value.
	# The brackets are the two keys the prototype in `test_track.gd` used for
	# exactly this, which keeps the muscle memory and is now honest about what it
	# does — the same two keys, moving whichever row is selected, through the
	# registry that records it.
	actions["tune_toggle"] = _action(DEADZONE_BUTTON, [_key(KEY_F2)])

	actions["tune_prev"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_DPAD_UP),
		_key(KEY_PAGEUP),
	])

	actions["tune_next"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_DPAD_DOWN),
		_key(KEY_PAGEDOWN),
	])

	actions["tune_decrease"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_DPAD_LEFT),
		_key(KEY_BRACKETLEFT),
	])

	actions["tune_increase"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_DPAD_RIGHT),
		_key(KEY_BRACKETRIGHT),
	])

	# Keyboard only, all three. Resetting a constant, saving a preset and
	# acknowledging a defended override are deliberate acts, and a deliberate act
	# does not belong on a surface a thumb rests against.
	actions["tune_reset"] = _action(DEADZONE_BUTTON, [_key(KEY_BACKSPACE)])
	actions["tune_save"] = _action(DEADZONE_BUTTON, [_key(KEY_F6)])
	actions["tune_unlock"] = _action(DEADZONE_BUTTON, [_key(KEY_U)])

	return actions


func _action(deadzone: float, events: Array) -> Dictionary:
	return {"deadzone": deadzone, "events": events}


func _key(keycode: Key) -> InputEventKey:
	var event := InputEventKey.new()
	# Physical, so WASD stays where the fingers are on AZERTY and Dvorak.
	event.physical_keycode = keycode
	return event


func _joy_button(button: JoyButton) -> InputEventJoypadButton:
	var event := InputEventJoypadButton.new()
	event.button_index = button
	# -1 means any device, so a pad reconnecting on a new index keeps working.
	event.device = -1
	return event


## axis_value carries the sign: -1.0 binds the negative half of the axis, +1.0 the
## positive half. Triggers only ever use +1.0 — they rest at -1.0 on some pads and
## Godot normalizes that before it reaches here.
func _joy_axis(axis: JoyAxis, axis_value: float) -> InputEventJoypadMotion:
	var event := InputEventJoypadMotion.new()
	event.axis = axis
	event.axis_value = axis_value
	event.device = -1
	return event
