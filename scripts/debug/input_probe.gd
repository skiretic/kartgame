extends Control

## M0 acceptance tool: proves the C++ extension loaded and shows live gamepad state.
##
## This is the interactive half of the M0 gate. The headless half is
## tools/verify/verify_extension.gd, which CI can run without a window.
##
## The gamepad readout exists because "DualSense works" is not one claim. A pad
## can enumerate over USB and not over Bluetooth, report buttons but not analog
## triggers, or land on a different SDL mapping on each transport. The only honest
## check is watching the raw axes move, which is what this shows.

## Raw axes are polled every frame rather than driven by input events, because an
## axis resting inside its deadzone emits no events and would otherwise read as
## absent rather than as centered.
const WATCHED_AXES: Array[JoyAxis] = [
	JOY_AXIS_LEFT_X,
	JOY_AXIS_LEFT_Y,
	JOY_AXIS_RIGHT_X,
	JOY_AXIS_RIGHT_Y,
	JOY_AXIS_TRIGGER_LEFT,
	JOY_AXIS_TRIGGER_RIGHT,
]

const AXIS_NAMES := {
	JOY_AXIS_LEFT_X: "Left stick X",
	JOY_AXIS_LEFT_Y: "Left stick Y",
	JOY_AXIS_RIGHT_X: "Right stick X",
	JOY_AXIS_RIGHT_Y: "Right stick Y",
	JOY_AXIS_TRIGGER_LEFT: "L2 (brake)",
	JOY_AXIS_TRIGGER_RIGHT: "R2 (throttle)",
}

## Analog actions are shown as a float; the rest as pressed or not. Splitting them
## here keeps the readout honest about which inputs actually carry a magnitude.
const ANALOG_ACTIONS: Array[StringName] = [
	&"throttle",
	&"brake",
	&"steer_left",
	&"steer_right",
]

const DIGITAL_ACTIONS: Array[StringName] = [
	&"shift_up",
	&"shift_down",
	&"clutch",
	&"look_back",
	&"respawn",
	&"camera_cycle",
	&"pause",
]

var _build_label: RichTextLabel
var _pad_label: RichTextLabel
var _action_label: RichTextLabel


func _ready() -> void:
	_build_ui()
	_report_build_info()

	Input.joy_connection_changed.connect(_on_joy_connection_changed)

	# Every pad already present, printed once at startup.
	#
	# `joy_connection_changed` fires on a *change*, so a pad that was paired
	# before the scene launched — the normal case — announces itself nowhere.
	# That left M0's last acceptance item with its evidence visible only in a
	# window: the GUID was on screen and could not be pasted into an issue.
	for device: int in Input.get_connected_joypads():
		_report_pad(device, "present")


func _process(_delta: float) -> void:
	_pad_label.text = _describe_pads()
	_action_label.text = _describe_actions()


## Printed as well as displayed: when this scene is run from a terminal the
## console output is the artifact worth pasting into a bug report.
func _report_build_info() -> void:
	if not ClassDB.class_exists("KartCore"):
		var message := (
			"[color=red]KartCore not found.[/color]\n"
			+ "The GDExtension did not load. Build it with:\n"
			+ "    scons target=editor"
		)
		_build_label.text = message
		push_error("GDExtension not loaded: KartCore is not registered.")
		return

	var info: Dictionary = KartCore.build_info()
	var kz: Dictionary = KartCore.kz_reference()

	# A round trip through src/core/, which is compiled without godot-cpp. If this
	# returns the right number, the engine-free core is genuinely reachable from
	# GDScript through the extension boundary.
	var one_hundred_kmh_in_ms: float = KartCore.kmh_to_ms(100.0)

	var lines := PackedStringArray()
	lines.append("[b]C++ extension loaded[/b]")
	lines.append("  version         %s" % info["extension_version"])
	lines.append("  built against   Godot %s API" % info["godot_api_version"])
	lines.append("  running on      Godot %s" % Engine.get_version_info()["string"])
	lines.append("  target          %s / %s / %s" % [info["build_platform"], info["build_arch"], info["build_target"]])
	lines.append("  float precision %s" % info["float_precision"])
	lines.append("  physics tick    %d Hz" % Engine.physics_ticks_per_second)
	lines.append("")
	lines.append("[b]KZ reference envelope[/b] (from src/core/kz_reference.h)")
	lines.append("  mass            %.0f kg with driver" % kz["mass_with_driver_kg"])
	lines.append("  top speed       %.0f-%.0f km/h" % [kz["top_speed_min_kmh"], kz["top_speed_max_kmh"]])
	lines.append("  0-100 km/h      %.1f-%.1f s" % [kz["zero_to_100_kmh_min_s"], kz["zero_to_100_kmh_max_s"]])
	lines.append("  lateral         %.1f-%.1f g sustained, %.1f-%.1f peak" % [
			kz["lateral_sustained_g_min"], kz["lateral_sustained_g_max"],
			kz["lateral_peak_g_min"], kz["lateral_peak_g_max"]])
	lines.append("  powerband       %.0f-%.0f rpm, %d gears" % [kz["powerband_min_rpm"], kz["powerband_max_rpm"], kz["gear_count"]])
	lines.append("")
	lines.append("  100 km/h -> %.4f m/s (via src/core/units.h)" % one_hundred_kmh_in_ms)

	_build_label.text = "\n".join(lines)
	print("\n".join(lines).replace("[b]", "").replace("[/b]", ""))


func _describe_pads() -> String:
	var pads := Input.get_connected_joypads()
	if pads.is_empty():
		return "[b]Gamepads[/b]\n  [color=gray]none connected[/color]"

	var lines := PackedStringArray()
	lines.append("[b]Gamepads[/b]")

	for device: int in pads:
		lines.append("")
		lines.append("  [%d] %s" % [device, Input.get_joy_name(device)])
		# The GUID identifies the SDL mapping in use. USB and Bluetooth report
		# different GUIDs for the same physical pad, so this is how you confirm
		# both transports were actually exercised rather than the same one twice.
		lines.append("      guid   %s" % Input.get_joy_guid(device))
		lines.append("      vibrate %s" % ("yes" if Input.get_joy_vibration_duration(device) >= 0.0 else "unknown"))

		for axis: JoyAxis in WATCHED_AXES:
			var value := Input.get_joy_axis(device, axis)
			lines.append("      %-14s %+.3f %s" % [AXIS_NAMES[axis], value, _bar(value)])

		var pressed := PackedStringArray()
		for button: int in range(JOY_BUTTON_MAX):
			if Input.is_joy_button_pressed(device, button):
				pressed.append(str(button))
		lines.append("      buttons down   %s" % ("-" if pressed.is_empty() else ", ".join(pressed)))

	return "\n".join(lines)


func _describe_actions() -> String:
	var lines := PackedStringArray()
	lines.append("[b]Actions[/b]")

	# Steering is two half-axis actions combined into one signed value; this is the
	# same expression the vehicle solver will use.
	var steer := Input.get_action_strength(&"steer_right") - Input.get_action_strength(&"steer_left")
	lines.append("  %-14s %+.3f %s" % ["steer", steer, _bar(steer)])

	for action: StringName in ANALOG_ACTIONS:
		if not InputMap.has_action(action):
			lines.append("  [color=red]%s missing from the input map[/color]" % action)
			continue
		var strength := Input.get_action_strength(action)
		lines.append("  %-14s %+.3f %s" % [action, strength, _bar(strength)])

	lines.append("")
	var down := PackedStringArray()
	for action: StringName in DIGITAL_ACTIONS:
		if not InputMap.has_action(action):
			lines.append("  [color=red]%s missing from the input map[/color]" % action)
			continue
		if Input.is_action_pressed(action):
			down.append(String(action))
	lines.append("  held           %s" % ("-" if down.is_empty() else ", ".join(down)))

	return "\n".join(lines)


func _bar(value: float) -> String:
	var width := 20
	var filled := int(round(absf(value) * width))
	return "[" + "#".repeat(filled) + ".".repeat(width - filled) + "]"


func _on_joy_connection_changed(device: int, connected: bool) -> void:
	# Printed rather than only displayed, so a Bluetooth pad that drops and
	# reconnects mid-session leaves a trace in the log.
	if connected:
		_report_pad(device, "connected")
	else:
		print("joypad %d disconnected: %s" % [device, Input.get_joy_name(device)])


## One pad, to stdout, with the GUID.
##
## The GUID is the *evidence* M0's last acceptance item asks for: USB and
## Bluetooth report different GUIDs for the same physical pad, so two runs
## printing two GUIDs are the proof that two transports were exercised rather
## than one tested twice. A number that only ever appears in a window cannot be
## pasted into an issue, which is what closing that item requires.
func _report_pad(device: int, state: String) -> void:
	print("joypad %d %s: %s" % [device, state, Input.get_joy_name(device)])
	print("      guid %s" % Input.get_joy_guid(device))


func _build_ui() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)

	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 16)
	margin.add_theme_constant_override("margin_top", 16)
	margin.add_theme_constant_override("margin_right", 16)
	margin.add_theme_constant_override("margin_bottom", 16)
	add_child(margin)

	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 24)
	margin.add_child(columns)

	_build_label = _make_panel(columns)
	_pad_label = _make_panel(columns)
	_action_label = _make_panel(columns)


func _make_panel(parent: Node) -> RichTextLabel:
	var label := RichTextLabel.new()
	label.bbcode_enabled = true
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.size_flags_vertical = Control.SIZE_EXPAND_FILL
	# Monospace, so the bar graphs and the +0.000 columns line up.
	label.add_theme_font_override("normal_font", ThemeDB.fallback_font)
	parent.add_child(label)
	return label
