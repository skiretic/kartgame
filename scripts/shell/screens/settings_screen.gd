extends ShellScreen

## Plate 9. Comfort, assists and audio, as one grouped paper form.
##
## ## Every row on this screen persists, and that is the design constraint
##
## `GAMEDESIGN.md` §13 forbids a claim the code does not honor, and a settings
## screen is the easiest place in a game to break that rule: a slider that moves,
## looks right, and is forgotten the moment the process ends. So the rule here is
## mechanical rather than a good intention — **a row exists only if
## `KartSettings.save()` writes it and `load()` reads it back** — and
## `shell_probe.gd`'s `every settings row round-trips` check is what enforces it,
## by moving every field the C++ side exposes, saving, reloading into a fresh
## instance and comparing.
##
## Two things on plate 9 are therefore **not** here, and their absence is the
## honest answer rather than an omission:
##
##   * **Engine and effects volume.** The plate draws three audio sliders and the
##     format has one, `master_volume_db`. ADR-0039 trimmed Master and made that
##     number a preference; there is no per-bus preference in `settings.h` and
##     nothing reads one, so offering two more would be exactly the decorative
##     slider. They arrive when the buses do.
##   * **Input remapping.** ARCHITECTURE §18 asks for it and it is a screen, not a
##     value — `project.godot`'s `[input]` section is the store and there is no
##     `settings.cfg` key it could round-trip through.
##
## ## Why a press cycles a value instead of left/right moving it
##
## `screen_stack.gd` consumes `menu_left` and `menu_right` for **focus**
## navigation and its header argues at length for there being exactly one focus
## walker — reading the directions anywhere else means the engine's `ui_*` walk and
## the stack's walk both move the selection, and the second move is invisible in
## the code that causes it. There is no left/right value hook, and adding a second
## reader of those actions here would be the bug that header exists to prevent.
##
## So a row is a `Button` and a press steps it: booleans toggle, numbers advance
## one notch and wrap at the top. That is the pad-menu pattern rather than a
## compromise — every row is one action away from any value it can hold, `Cross`
## and `Enter` and a mouse click all do the same thing, and nothing has to know
## about an axis.
##
## **The footer does not say left/right, and the plate does.** Keeping the plate's
## wording was the first instinct and it was wrong: a footer is an advertised
## control list, and advertising two controls that do nothing — left/right, and a
## backspace nothing binds — is the exact failure `control_hints.gd` heads. See
## `FOOTER` below. `setup_screen.gd` faces the identical problem and made the same
## choice; if `screen_stack.gd` ever grows a value hook, `_step` below is the one
## function that has to learn a direction, and the footer changes with it.
##
## ## Where the values go
##
## Straight into the `KartSettings` the shell already loaded, and to disk on every
## press. Not on exit: a preference that only survives a clean shutdown is a
## preference that does not survive the way people actually close a game, which is
## the argument `assist_settings.gd` makes for saving from the toggle.

## A row: the label, the getter and setter on `KartSettings`, and how a press
## moves it.
##
## `values` is the cycle for a stepped row and is empty for a toggle. The steps are
## coarse on purpose — a pad menu with 41 field-of-view positions is a menu nobody
## reaches the end of — and each list ends where `src/core/settings.h` clamps, so a
## press can never produce a value the setter has to pull back.
const ROWS := [
	{"group": "comfort"},
	{
		"label": "Field of view", "getter": "get_field_of_view_deg",
		"setter": "set_field_of_view_deg", "unit": "deg", "signed": true,
		"values": [-20.0, -15.0, -10.0, -5.0, 0.0, 5.0, 10.0, 15.0, 20.0],
	},
	{
		"label": "Head motion", "getter": "get_head_motion", "setter": "set_head_motion",
		"values": [0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0],
	},
	{
		"label": "Shake", "getter": "get_shake", "setter": "set_shake",
		"values": [0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0],
	},
	{"label": "Horizon lock", "getter": "is_horizon_lock", "setter": "set_horizon_lock"},
	{
		"label": "Motion blur", "getter": "get_motion_blur", "setter": "set_motion_blur",
		"values": [0.0, 0.5, 1.0, 1.5, 2.0],
	},

	{"group": "assists"},
	{"label": "Auto clutch", "getter": "is_auto_clutch", "setter": "set_auto_clutch"},
	{"label": "Auto shift", "getter": "is_auto_shift", "setter": "set_auto_shift"},
	{
		"label": "Pause invalidates lap", "getter": "is_pause_invalidates_lap",
		"setter": "set_pause_invalidates_lap", "note": "ADR-0052",
	},

	{"group": "audio"},
	{
		"label": "Master volume", "getter": "get_master_volume_db",
		"setter": "set_master_volume_db", "unit": "dB", "signed": true,
		"values": [-60.0, -48.0, -36.0, -24.0, -18.0, -12.0, -6.0, -3.0, 0.0, 3.0, 6.0],
	},
]

## Steering deadzone is in the format and is not on this screen: it belongs with
## the controls, which §18 pairs with remapping, and there is no controls screen
## yet. It is listed here so the omission is a decision on the record rather than
## something nobody noticed — `shell_probe.gd` proves the *format* round-trips it
## either way.
const DEFERRED_ROWS := ["steer_deadzone"]

const CARD_WIDTH := 440.0
const VALUE_WIDTH := 96.0
## What the screen actually does, which is not what the plate's footer says.
##
## Mockup plate 9 reads *"d-pad up/down selects · left/right moves · backspace
## resets"*, and **two of those three are not true here**. `ScreenStack` owns
## `menu_left`/`menu_right` for focus navigation and its header argues at length
## for exactly one walker, so a value steps by being **pressed** — Cross, Enter or
## a click, through `_confirm()`'s `emit_signal("pressed")` fallback. Nothing
## binds backspace at all.
##
## The plate was drawn before the input context existed and it is a mockup, not a
## contract. Shipping its wording would have put this screen in the family
## `control_hints.gd` heads — `look_back` bound and read by nothing, a pad with no
## on-screen controls, four cases in one day — where the list on the screen and
## the list in the code had drifted and the driver believed the screen. A footer
## is an advertised control list and it is held to the same standard as the
## InputMap.
##
## If a value stepper on left/right is wanted later it needs a hook on
## `ScreenStack`, not a second reader here.
const FOOTER := "d-pad or stick up/down selects  -  Cross cycles a value  -  Circle goes back"

var _rows: Array[Dictionary] = []
var _first_row: Button


func title() -> String:
	return "settings"


func build() -> void:
	add_child(ShellTheme.ground(true))

	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	for side: String in ["left", "right"]:
		margin.add_theme_constant_override("margin_" + side, 64)
	for side: String in ["top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 44)
	add_child(margin)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 4)
	column.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	column.custom_minimum_size = Vector2(CARD_WIDTH, 0.0)
	margin.add_child(column)

	# The titleblock: a heavy rule under the heading is most of what makes a sheet
	# read as a federation document rather than as a table. `ShellTheme.rule(true)`
	# is that weight.
	column.add_child(ShellTheme.label("Settings", ShellTheme.T_TITLE, ShellTheme.PAPER_INK,
			ShellTheme.Weight.BOLD))
	column.add_child(ShellTheme.rule(true, true))
	column.add_child(_spacer(10))

	for entry: Dictionary in ROWS:
		if entry.has("group"):
			column.add_child(_group_head(String(entry["group"])))
			continue
		column.add_child(_value_row(entry))

	column.add_child(_spacer(12))
	column.add_child(ShellTheme.rule(false, true))
	column.add_child(_spacer(4))
	column.add_child(ShellTheme.kicker(FOOTER, ShellTheme.PAPER_SOFT, ShellTheme.T_FOOT))

	_refresh()


func initial_focus() -> Control:
	return _first_row


# --- building -----------------------------------------------------------------


## An uppercase kicker over a 1 px rule. Plate 9's group separator, and the one
## place the two weights of `ShellTheme.rule` differ visibly on this screen.
func _group_head(text: String) -> Control:
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 2)
	box.add_child(_spacer(10))
	box.add_child(ShellTheme.kicker(text, ShellTheme.PAPER_SOFT))
	box.add_child(ShellTheme.rule(false, true))
	box.add_child(_spacer(2))
	return box


## Label on the left, value on the right, and the **button is the whole row** —
## `size_flags_horizontal = EXPAND_FILL` — so the focus wash and the accent border
## mark the row rather than a word in it. Same shape as `paddock_screen.gd`'s mode
## rows, deliberately.
func _value_row(entry: Dictionary) -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)

	var button := ShellTheme.row_button(String(entry["label"]), ShellTheme.PAPER_INK,
			ShellTheme.T_ROW)
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(button)

	if entry.has("note"):
		row.add_child(ShellTheme.kicker(String(entry["note"]), ShellTheme.PAPER_SOFT))

	# Not a `Button`, so the walk in `shell_screen.gd` sees one focusable per row
	# and check 5's BFS does not have to reach a control that cannot be pressed.
	var value := ShellTheme.label("", ShellTheme.T_ROW, ShellTheme.PAPER_INK,
			ShellTheme.Weight.SEMIBOLD)
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	value.custom_minimum_size = Vector2(VALUE_WIDTH, 0.0)
	row.add_child(value)

	var record := {"entry": entry, "button": button, "value": value}
	_rows.append(record)
	button.pressed.connect(_step.bind(record))
	if _first_row == null:
		_first_row = button
	return row


static func _spacer(height: int) -> Control:
	var node := Control.new()
	node.custom_minimum_size = Vector2(0.0, float(height))
	return node


# --- reading and writing ------------------------------------------------------


## The shell's `KartSettings`, or null when the extension is not built.
##
## `shell` is typed `Node` rather than `ShellRoot` — typing it would close a class
## cycle, which `shell_screen.gd` says in its own words — so this call returns
## Variant and the local has to be typed here or GDScript will not parse the file.
func _settings() -> KartSettings:
	if shell == null or not shell.has_method("settings"):
		return null
	var found: KartSettings = shell.call("settings")
	return found


## Advance one row and write the file.
##
## Saving on every press rather than on exit: `assist_settings.gd` makes the
## argument — a preference that only survives a clean shutdown is a preference that
## does not survive the way people actually close a game — and a settings file is
## twelve lines through the same temp-then-rename-then-fsync path the career uses,
## so the cost is nothing a person can perceive.
func _step(record: Dictionary) -> void:
	var settings := _settings()
	if settings == null:
		return
	var entry: Dictionary = record["entry"]
	var getter := String(entry["getter"])
	var setter := String(entry["setter"])

	if entry.has("values"):
		var values: Array = entry["values"]
		var current := float(settings.call(getter))
		settings.call(setter, values[(_nearest(values, current) + 1) % values.size()])
	else:
		settings.call(setter, not bool(settings.call(getter)))

	settings.save()
	_refresh()


## Which step a stored value is at.
##
## By nearest rather than by equality, because the file is hand-editable and a
## `head_motion 0.6` typed in by a person must land somewhere sensible instead of
## snapping the row to its first entry on the next press. `format_value` writes six
## decimals and `parse_value` reads them back exactly, so a value this screen wrote
## always matches its own step and the nearest search is a no-op on it.
static func _nearest(values: Array, current: float) -> int:
	var best := 0
	var best_gap := INF
	for index: int in values.size():
		var gap := absf(float(values[index]) - current)
		if gap < best_gap:
			best_gap = gap
			best = index
	return best


func _refresh() -> void:
	var settings := _settings()
	for record: Dictionary in _rows:
		var entry: Dictionary = record["entry"]
		var label: Label = record["value"]
		if settings == null:
			# The extension is not built. Say so once per row rather than drawing
			# plausible numbers nothing is behind.
			label.text = "--"
			var button: Button = record["button"]
			button.disabled = true
			button.focus_mode = Control.FOCUS_NONE
			continue
		var current: Variant = settings.call(String(entry["getter"]))
		label.text = _format(entry, current)


## What a value reads as on the sheet.
##
## Numbers get one decimal at most and drop it when they are whole, because a row
## saying `1.0` beside a row saying `Off` is a form that cannot decide what it is.
## A signed row keeps its sign at zero suppressed and shows `+` above it, which is
## what makes a trim read as a trim rather than as an absolute.
static func _format(entry: Dictionary, current: Variant) -> String:
	if current is bool:
		return "On" if bool(current) else "Off"
	var number := float(current)
	if not entry.has("unit") and is_zero_approx(number):
		return "Off"
	var text := ("%.1f" % number) if absf(number - roundf(number)) > 0.001 \
			else ("%d" % int(roundf(number)))
	if bool(entry.get("signed", false)) and number > 0.0:
		text = "+" + text
	return text if not entry.has("unit") else "%s %s" % [text, entry["unit"]]
