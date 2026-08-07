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
## Re-running is safe, and "safe" had to be rebuilt to be true. Two things were
## wrong at once:
##
## 1. The source had drifted from its own output. Commit 6b2dd32 — the driver's
##    pad layout, shifts on the face buttons and clutch on R1 — edited
##    project.godot directly and never came back here, so re-running this script
##    would have quietly reverted Square/Cross/R1/R3 to the shoulder-button
##    layout nobody uses, and `auto_shift_toggle` appeared in this file zero
##    times. A generator whose output is edited by hand is not a generator.
##
## 2. `ProjectSettings.save()` rewrites the whole file from the in-memory
##    property list, which **erases every comment** and **drops any setting equal
##    to its engine default**. Measured: 26 lines gone, including the entire
##    `[display]` block explaining why the window is 1600x900, and
##    `window/dpi/allow_hidpi=true` deleted outright. Those comments are the
##    project's record of decisions that cost real time to make.
##
## So the write is a splice, not a save. `ProjectSettings.save()` still does the
## serialization — that is the whole point of generating it — but it writes to a
## scratch copy, and only the `[input]` section's text is transplanted back into
## the committed file. Everything outside `[input]` is preserved byte for byte.
##
## This script **owns the whole `[input]` section**: an action present in
## project.godot and absent here is deleted, which is what makes the readable
## source actually the source. Engine `ui_*` actions are untouched because they
## are defaults and never appear in the file.
##
## The acceptance check is that running it on a clean tree produces no diff:
##
##     godot --headless --path . --script tools/setup/generate_input_map.gd
##     git diff --stat project.godot        # must be empty
##
## Gamepad indices are SDL's, which Godot inherits along with the SDL controller
## database. That is what makes the same mapping correct on a DualSense, an Xbox
## pad, and a generic clone without per-device special cases — see
## ARCHITECTURE.md §9.

const PROJECT_FILE := "res://project.godot"

const DEADZONE_STICK := 0.15

## Triggers rest at zero and are pressed one way only, so their deadzone only has
## to reject sensor noise. A stick deadzone here would eat real throttle travel.
const DEADZONE_TRIGGER := 0.05

const DEADZONE_BUTTON := 0.5

## A menu is navigated in discrete steps, so the stick has to *latch* rather than
## report travel: at the 0.15 stick deadzone a thumb resting on a DualSense walks
## the selection on its own. 0.5 is high enough that only a deliberate push
## counts, and the d-pad buttons in the same actions report 1.0 either way.
const DEADZONE_MENU_STICK := 0.5


func _initialize() -> void:
	var actions := _action_definitions()

	var original := _read(PROJECT_FILE)
	if original.is_empty():
		push_error("Could not read %s" % PROJECT_FILE)
		quit(1)
		return

	# Own the section: anything the file carries that this source no longer
	# defines is erased. Only names actually written in the file are considered,
	# so the engine's ui_* defaults — which live in ProjectSettings but never on
	# disk — are left exactly where they are.
	var stale := 0
	for action_name: String in _action_names_in_section(original):
		if not actions.has(action_name):
			ProjectSettings.set_setting("input/" + action_name, null)
			stale += 1

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

	# ProjectSettings.save() has just flattened the file. Take the one section it
	# was asked to produce and put the rest of the document back.
	var generated := _read(PROJECT_FILE)
	var block := _section_text(generated, "input")
	if block.is_empty():
		push_error("Generated project.godot has no [input] section")
		quit(1)
		return

	err = _write(PROJECT_FILE, _replace_section(original, "input", block))
	if err != OK:
		push_error("Failed to write %s: %d" % [PROJECT_FILE, err])
		quit(1)
		return

	print("Wrote %d input actions to project.godot (%d stale removed)"
			% [actions.size(), stale])
	quit(0)


func _read(path: String) -> String:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return ""
	var text := f.get_as_text()
	f.close()
	return text


func _write(path: String, text: String) -> int:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return FileAccess.get_open_error()
	f.store_string(text)
	f.close()
	return OK


## The names on the left of `name={` inside a section. Godot writes each action
## as a multi-line dictionary, so only lines at column zero that end in `={` and
## sit between the section header and the next one count.
func _action_names_in_section(text: String) -> PackedStringArray:
	var names := PackedStringArray()
	var inside := false
	for line: String in text.split("\n"):
		if line.begins_with("["):
			inside = line.strip_edges() == "[input]"
			continue
		if not inside:
			continue
		var eq := line.find("=")
		if eq > 0 and not line.begins_with(" ") and not line.begins_with("\t"):
			names.append(line.substr(0, eq))
	return names


## Everything from `[<name>]` to the line before the next `[section]`, header
## included, with trailing blank lines trimmed. Empty if the section is absent.
func _section_text(text: String, section: String) -> String:
	var header := "[" + section + "]"
	var lines := text.split("\n")
	var out := PackedStringArray()
	var inside := false
	for line: String in lines:
		if line.begins_with("["):
			if inside:
				break
			inside = line.strip_edges() == header
		if inside:
			out.append(line)
	while out.size() > 0 and out[out.size() - 1].strip_edges().is_empty():
		out.remove_at(out.size() - 1)
	return "\n".join(out)


## Swap one section's text for another, keeping every other byte — comments
## included. Appends the section if the document does not already have one.
func _replace_section(text: String, section: String, block: String) -> String:
	var header := "[" + section + "]"
	var lines := text.split("\n")
	var out := PackedStringArray()
	var replaced := false
	var skipping := false
	for line: String in lines:
		if line.begins_with("["):
			if skipping:
				skipping = false
			elif line.strip_edges() == header:
				out.append_array(block.split("\n"))
				out.append("")
				replaced = true
				skipping = true
				continue
		if not skipping:
			out.append(line)
	if not replaced:
		out.append(block)
		out.append("")
	return "\n".join(out)


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
	#
	# **Both shifts are on the face buttons, not the shoulders** — Anthony's own
	# layout, 6b2dd32. Square up, Cross down, so both directions are under the
	# right thumb and the hand never leaves the stick. That puts the clutch on R1,
	# a free finger, and leaves **L1 deliberately unbound**. This is the layout in
	# `scripts/game/control_hints.gd` and in CLAUDE.md's control list; the three
	# lists are the same list and drift between them is the bug this file exists
	# to prevent.
	actions["shift_up"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_X),
		_key(KEY_E),
	])

	actions["shift_down"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_A),
		_key(KEY_Q),
	])

	# A DualSense has no third analog axis free, so on a pad the clutch is digital
	# and auto-clutch carries the launch. An analog clutch arrives with pedal sets,
	# which is why the sim reads this as a float rather than a bool.
	actions["clutch"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_RIGHT_SHOULDER),
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

	# R3, because it is the last surface on a DualSense that is not already a
	# control and pressing a stick down is hard to do by accident. The assist is
	# a per-driver preference rather than a debug key, so unlike F2..F6 it gets a
	# pad binding. Persisted through `scripts/game/assist_settings.gd`.
	actions["auto_shift_toggle"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_RIGHT_STICK),
		_key(KEY_G),
	])

	actions["camera_cycle"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_BACK),
		_key(KEY_V),
	])

	actions["pause"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_START),
		_key(KEY_ESCAPE),
	])

	# --- Menus (ADR-0053 §2) -------------------------------------------------
	#
	# **Every one of these collides with a driving action, and that is deliberate
	# rather than an oversight.** A pad has no spare buttons — the driving map
	# above already spends both triggers, both sticks, all four face buttons, R1,
	# Create, Options and the whole d-pad. Cross confirms *and* shifts down;
	# Circle backs out *and* respawns; the d-pad walks a menu *and* moves a tuning
	# value. There is no arrangement of a DualSense where that is not true.
	#
	# So the collision is resolved by a mechanism instead of by a spare button,
	# and the obvious mechanism does not work. **`set_input_as_handled()` cannot
	# stop the kart**: `KartBody::gather_input` polls `Input.get_action_strength`
	# in `_physics_process` rather than reading the event queue, so a menu that
	# consumed the event would still have applied full throttle underneath it.
	# The same fact is written out at length in the tuning block below, and it is
	# what killed the arrow keys as an overlay binding.
	#
	# The lever that does work is `PlayerDriver.enabled`, whose single writer is
	# `SessionRunner._apply_input_gate()`. A menu opened over a live session goes
	# through `SessionRunner.set_input_suspended(true)`, which stops the driver
	# from producing a `DriverInput` at all — so Cross reaches the gearbox only
	# when no menu is up. In the shell proper there is no `PlayerDriver` in the
	# tree at all, and Cross reaches nothing by construction.
	#
	# `tools/verify/shell_probe.gd` check 8 asserts the overlap set against a
	# written-down list, so a *new* collision fails the gate rather than being
	# discovered by a driver whose kart downshifted when they picked a track.
	actions["menu_confirm"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_A),
		_key(KEY_ENTER),
	])

	actions["menu_back"] = _action(DEADZONE_BUTTON, [
		_joy_button(JOY_BUTTON_B),
		_key(KEY_ESCAPE),
	])

	# The d-pad and the left stick both navigate, because a driver reaching for a
	# menu has a thumb on one or the other and should not have to find out which.
	# The arrow keys are the keyboard half: they are the second binding on
	# throttle/brake/steer, which is fatal for an *overlay* over a running kart —
	# the tuning overlay had to give them up for exactly that reason — but a menu
	# suspends the driver outright, so nothing is polling them while one is open.
	actions["menu_up"] = _action(DEADZONE_MENU_STICK, [
		_joy_button(JOY_BUTTON_DPAD_UP),
		_joy_axis(JOY_AXIS_LEFT_Y, -1.0),
		_key(KEY_UP),
	])

	actions["menu_down"] = _action(DEADZONE_MENU_STICK, [
		_joy_button(JOY_BUTTON_DPAD_DOWN),
		_joy_axis(JOY_AXIS_LEFT_Y, 1.0),
		_key(KEY_DOWN),
	])

	actions["menu_left"] = _action(DEADZONE_MENU_STICK, [
		_joy_button(JOY_BUTTON_DPAD_LEFT),
		_joy_axis(JOY_AXIS_LEFT_X, -1.0),
		_key(KEY_LEFT),
	])

	actions["menu_right"] = _action(DEADZONE_MENU_STICK, [
		_joy_button(JOY_BUTTON_DPAD_RIGHT),
		_joy_axis(JOY_AXIS_LEFT_X, 1.0),
		_key(KEY_RIGHT),
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
