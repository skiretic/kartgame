extends ShellScreen

## Plate 6. The session setup sheet, in the paper-document family: an entry form.
##
## A titleblock over a two-column body over a footer, which is the shape of a
## federation entry form and not of a settings page. The left column is the form
## itself, the right column is the circuit in plan, and the 2 px rules top and
## bottom are what make it read as a document rather than as a dialog.
##
## ## Nothing on this sheet is typed
##
## The circuit's name, its length, its corner count, its elevation range, its
## layout names and the map are all read from `KartTrack` against
## `data/tracks/valdirone_nuova.track.json`. ADR-0046 makes that file the whole
## track and `circuit.sh` already measures the two geometry consumers against each
## other; a menu that carried its own copy of "1,375 m" would be a third consumer
## nothing checks, and it would be wrong the first time a corner moved.
##
## The assists are read from `KartSettings` and shown dimmed, because the Settings
## screen owns them — the plate greys those two rows for exactly that reason. A
## second writer of a stored preference is the `assist_settings.gd` failure
## CLAUDE.md already has a paragraph about.
##
## ## Why a value row cycles on confirm rather than stepping on left/right
##
## The plate draws `forward · reverse` and `best · off`, which reads as a
## left/right stepper. It is not one, and the reason is structural rather than a
## shortcut: `screen_stack.gd` consumes `menu_left` and `menu_right` in `_input()`
## to walk **focus**, and its header spends thirty lines on why there is exactly
## one focus walker in this shell. There is no value hook in the stack, and adding
## a second reader of those two actions — or reading the `Input` singleton from a
## screen — is the bug that header exists to prevent.
##
## So a value row is a `Button` whose `pressed` cycles to the next value. That
## needs no new machinery at all: `ScreenStack._confirm()` already falls through to
## `emit_signal("pressed")` on a focused `BaseButton`, so Cross, Enter and a mouse
## click all do the same thing, and the row keeps the theme's focus wash and accent
## border without a subclass reimplementing them. With two values per row, cycling
## and stepping are the same act.
##
## ## The hand-off
##
## `shell.start_session()` is handed a Dictionary of strings in `circuit.gd`'s own
## argument vocabulary — `shell_probe.gd` check 11 asserts the key set is a subset
## of what that scene documents. Only what the form actually set is posted: a menu
## that echoed every default back as an explicit argument would make
## `SessionRequest`'s "the command line wins" merge meaningless, because there
## would be no unset key left for a command line to win.
##
## Before posting, a `KartSession` is built and asked `is_valid()`, and the clauses
## `SessionRunner._first_problem()` refuses on are re-checked here with the same
## sentences. `src/core/session.h` says `is_valid()` exists precisely so a menu and
## the runner can call the same predicate; a form that started a session the runner
## then refused would put the refusal three screens away from the control that
## caused it.

const TRACK_PATH := "res://data/tracks/valdirone_nuova.track.json"

## The only session type this screen offers, and it is not a placeholder for four
## more. `src/core/session.h`'s `session_has_field()` answers Practice **false**
## and the other four **true**, and `SessionRunner._first_problem()` refuses
## `entry_count > 1` by name because ADR-0047's roster and M7's AI are both
## prerequisites. So Practice is not "the one implemented so far" — it is the one
## whose entry list is legal today.
const SESSION_TYPE := "practice"

## `centerline()`'s two arguments, matched to `circuit.gd:293`'s own call so the
## map is decimated exactly the way the collider's debug draw is.
##
## The two limits bind on different parts of the lap and both are needed. Sagitta
## binds in the corners: at 0.2 m on Il Pozzo's 15 m radius — the tightest of the
## eight — a chord is 4.88 m and 18.7 degrees, and the map's line is never further
## than 20 cm from the asphalt, which at this scale is a fifth of the stroke. The
## 8 m spacing cap binds on the straights, where sagitta alone would collapse the
## 165 m start straight into one segment. Measured over the whole lap: 222 points,
## spacing 0.22 to 7.96 m, mean 6.22.
const MAP_SAGITTA := 0.2
const MAP_MAX_SPACING := 8.0

## Row height, pixels at the design canvas.
##
## **Derived from the plate**: its form table is `font-size: 0.85rem` — 13.6 px at
## the 16 px root `ShellTheme`'s scale is ported to, which is `T_ROW` — inside
## `.ptab td { padding: 0.38rem 0.6rem }`, so `14 * 1.2 + 2 * 6.1 = 29.0`.
##
## It has to be set explicitly because a value row is a `Button` with **empty
## text** carrying its labels as children — an empty button's minimum height is its
## font's, which is not the row's, and the whole column would collapse by a third.
const ROW_HEIGHT := 29.0

## The map's box, pixels at the design canvas.
##
## **Derived from the plate's proportion, not from its pixel size.** The mockup
## draws the SVG at `max-width: 15rem` = 240 px inside a `.wrap` of
## `max-width: 64rem` = 1024 px, so the map is 0.234 of the sheet's width. This
## sheet is `1600 - 2 * 72 = 1456` px wide at the design canvas, and
## `0.234 * 1456 = 341`. Copying the plate's 240 px outright would have shrunk the
## map to a sixth of the sheet, which is the mistake `shell_theme.gd`'s type-scale
## comment already records for the HUD.
##
## The height comes from the plate's own `viewBox="220 30 560 660"`: aspect
## `560 / 660 = 0.848`, so `341 / 0.848 = 402`. Neither axis fills the column — the
## map and its caption are one centered group, which is where the plate puts them.
## A map set to expand vertically pushed its caption to the floor of the sheet, a
## third of a screen away from the thing it captions.
const MAP_WIDTH := 341.0
const MAP_HEIGHT := 402.0


## One row's value set. Not a `Button` subclass: the row itself is a plain
## `ShellTheme.row_button`, so the focus styling stays owned by the theme, and this
## holds the state the button's `pressed` handler advances.
##
## `keys` is what gets posted and `labels` is what gets drawn. They are separate
## because the two are not always the same word — the layout row happens to post
## its own label, the ghost row posts a ghost id and draws "best".
class Choice extends RefCounted:
	var keys: PackedStringArray = PackedStringArray()
	var labels: PackedStringArray = PackedStringArray()
	var index := 0

	## Whether this row is a decision the driver made, as against a fact the sheet
	## is reporting. The plate bolds the first and leaves the second plain.
	var chooses := false

	## Drawn in `PAPER_SOFT` throughout: the two assist rows, which are owned by the
	## Settings screen and shown here for the record.
	var dim := false

	## A trailing sentence in `PAPER_SOFT`, for a row that cannot be changed and
	## owes a reason. The paddock's rule for an unbuilt mode, one level down.
	var note := ""

	var button: Button
	var values: HBoxContainer
	var note_label: Label

	static func of(label_texts: PackedStringArray,
			key_texts: PackedStringArray) -> Choice:
		var made := Choice.new()
		made.labels = label_texts
		made.keys = key_texts
		return made

	static func fixed(text: String) -> Choice:
		return Choice.of(PackedStringArray([text]), PackedStringArray([text]))

	func steps() -> bool:
		return keys.size() > 1

	func key() -> String:
		return keys[index] if index < keys.size() else ""

	func advance() -> void:
		if steps():
			index = (index + 1) % keys.size()


var _track: KartTrack

## Why the sheet cannot be an entry form at all — the track file did not load.
## Non-empty means the form is cut and the loader's own complaints are printed
## instead, which is the honest version of a sheet whose every field would be
## blank.
var _blocked := PackedStringArray()

var _layout := Choice.fixed("forward")
var _ghost := Choice.fixed("off")
var _go: Button
var _refusal: Label
var _stamp: Label
var _map: TrackMap
var _first_focus: Control


func title() -> String:
	return "setup"


func build() -> void:
	_load_track()

	add_child(ShellTheme.ground(true))

	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	for side: String in ["left", "right"]:
		margin.add_theme_constant_override("margin_" + side, 72)
	for side: String in ["top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 48)
	add_child(margin)

	var sheet := VBoxContainer.new()
	sheet.add_theme_constant_override("separation", 0)
	margin.add_child(sheet)

	sheet.add_child(_titleblock())
	sheet.add_child(ShellTheme.rule(true, true))
	sheet.add_child(_body())
	sheet.add_child(ShellTheme.rule(true, true))
	sheet.add_child(_footer())

	_refresh()


# --- the circuit ---------------------------------------------------------------


## Load the track once, here, and keep it for the life of the screen. Every figure
## on the sheet and the map itself come off this object.
func _load_track() -> void:
	if not ClassDB.class_exists("KartTrack"):
		_blocked.append("KartTrack is not registered — build the extension with "
				+ "`scons target=editor arch=arm64`")
		return
	_track = KartTrack.new()
	var error := _track.load(TRACK_PATH)
	if error != OK or not _track.is_loaded():
		# The loader's twenty rules, verbatim. `load` refuses on purpose — a track
		# that loads and cannot be raced fails three hundred meters into a session
		# instead — so the refusal is the useful thing to print, not a summary of it.
		_blocked.append("%s refused (%d)" % [TRACK_PATH.get_file(), error])
		for problem: String in _track.problems():
			_blocked.append(problem)
		_track = null
		return
	var names := _track.layout_names()
	if not names.is_empty():
		_track.select_layout(names[0])


## The circuit slug, spelled exactly as `circuit.gd:697` spells it — the file's
## stem, twice-stripped so `valdirone_nuova.track.json` becomes `valdirone_nuova`.
## Both `.basename()` calls are needed and the second one is the `.track` half.
##
## It has to match, character for character: it is the key a best lap is filed
## under in `KartProfile`, and a menu that offered a ghost under one slug while the
## session recorded under another would look like a profile that forgets laps.
func _slug() -> String:
	if _track == null:
		return ""
	return _track.source_path().get_file().get_basename().get_basename()


# --- titleblock ------------------------------------------------------------------


func _titleblock() -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 24)

	var left := VBoxContainer.new()
	left.add_theme_constant_override("separation", 3)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_child(ShellTheme.kicker("Entry form · %s" % SESSION_TYPE,
			ShellTheme.PAPER_SOFT))

	var circuit := _track.track_name() if _track != null else "no circuit"
	var heading := ShellTheme.label(
			"%s · %s" % [circuit, _class_name_upper()],
			ShellTheme.T_TITLE, ShellTheme.PAPER_INK, ShellTheme.Weight.BOLD)
	left.add_child(heading)
	row.add_child(left)

	# The plate's right-hand annotation, rewritten to say something true. Its own
	# words were "One screen for all four session types"; four are not offered and
	# the reason is `session_has_field()`, so the sheet says the reason instead of
	# advertising three sessions that `_first_problem()` would refuse.
	var meta := VBoxContainer.new()
	meta.add_theme_constant_override("separation", 2)
	for line: String in ["Practice is the only session that runs",
			"without a field, and there is no field yet"]:
		var note := ShellTheme.label(line, ShellTheme.T_FOOT, ShellTheme.PAPER_SOFT)
		note.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		meta.add_child(note)
	row.add_child(meta)

	var pad := MarginContainer.new()
	pad.add_theme_constant_override("margin_left", 20)
	pad.add_theme_constant_override("margin_right", 20)
	pad.add_theme_constant_override("margin_top", 16)
	pad.add_theme_constant_override("margin_bottom", 14)
	pad.add_child(row)
	return pad


## `KZ2`, not `kz2`. `KartSession.kart_class_name()` is the source and it returns
## the lowercase wire spelling; the sheet is the one place that renders it.
func _class_name_upper() -> String:
	return String(KartSession.kart_class_name(KartSession.CLASS_KZ2)).to_upper()


# --- body ------------------------------------------------------------------------


func _body() -> Control:
	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 0)
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL

	var form := _form()
	form.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	# The plate's `grid-template-columns: 1.2fr 1fr`, as stretch ratios.
	form.size_flags_stretch_ratio = 1.2
	columns.add_child(form)

	# The 1 px vertical divider. `ShellTheme.rule()` builds horizontal rules and
	# nothing more, so this is the same token at the other orientation rather than
	# a second color decision.
	var divider := ColorRect.new()
	divider.color = ShellTheme.PAPER_RULE
	divider.custom_minimum_size = Vector2(1.0, 0.0)
	divider.size_flags_vertical = Control.SIZE_EXPAND_FILL
	columns.add_child(divider)

	var right := _map_column()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_stretch_ratio = 1.0
	columns.add_child(right)
	return columns


func _form() -> Control:
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 0)

	if _track == null:
		for line: String in _blocked:
			var problem := ShellTheme.label(line, ShellTheme.T_BODY,
					ShellTheme.ST_SLOWER, ShellTheme.Weight.SEMIBOLD)
			problem.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			column.add_child(problem)
		return _sheet_pad(column)

	_layout = Choice.of(_track.layout_names(), _track.layout_names())
	_layout.chooses = true
	_ghost = _ghost_choice()

	var circuit := Choice.fixed(_track.track_name())
	circuit.chooses = true
	var kart_class := Choice.fixed(_class_name_upper())
	kart_class.chooses = true

	_add_row(column, "Circuit", circuit)
	_add_row(column, "Layout", _layout)
	_add_row(column, "Class", kart_class)
	_add_row(column, "Length", _length_choice())
	_add_row(column, "Ghost", _ghost)
	_add_row(column, "Tuning preset", _preset_choice())
	_add_row(column, "Auto clutch", _assist_choice(true))
	_add_row(column, "Auto shift", _assist_choice(false))

	# The plate prints a note here and its own text says it is not printed. What
	# does belong in the space is the refusal, because this is the column the
	# refusal is about.
	_refusal = ShellTheme.label("", ShellTheme.T_FOOT, ShellTheme.ST_SLOWER,
			ShellTheme.Weight.SEMIBOLD)
	_refusal.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_refusal.custom_minimum_size = Vector2(0.0, 26.0)
	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(0.0, 10.0)
	column.add_child(spacer)
	column.add_child(_refusal)
	return _sheet_pad(column)


## The plate's `.sheet { padding: 1rem 1.4rem 1.3rem }`, at a 16 px root.
static func _sheet_pad(inner: Control) -> Control:
	var pad := MarginContainer.new()
	pad.add_theme_constant_override("margin_left", 22)
	pad.add_theme_constant_override("margin_right", 22)
	pad.add_theme_constant_override("margin_top", 16)
	pad.add_theme_constant_override("margin_bottom", 21)
	pad.add_child(inner)
	return pad


## One form row: a full-width `Button` carrying its label and value as children.
##
## The children rather than the button's own `text` because the row is two-toned —
## the name in ink on the left, the value right-aligned with the inactive token
## dimmed — and a `Button` draws one string in one color. Putting them inside the
## button rather than beside it is what makes the focus wash and the accent left
## border cover the whole row, which is how the plate marks a selection.
func _add_row(column: VBoxContainer, name_text: String, choice: Choice) -> void:
	var ink := ShellTheme.PAPER_SOFT if choice.dim else ShellTheme.PAPER_INK
	var button := ShellTheme.row_button("", ink, ShellTheme.T_ROW)
	button.custom_minimum_size = Vector2(0.0, ROW_HEIGHT)
	# Connected unconditionally, and reachability is carried by `disabled` alone.
	# The ghost row's value set is a function of the layout, so a row that has one
	# value now can have two after the next press and back again — wiring the signal
	# only for rows that step today would leave that row permanently dead.
	button.pressed.connect(_on_row_pressed.bind(choice))
	if choice.steps() and _first_focus == null:
		_first_focus = button

	var line := HBoxContainer.new()
	line.set_anchors_preset(Control.PRESET_FULL_RECT)
	# Inside `_row_box`'s own 8 px content margins, so the text does not sit under
	# the accent border a focused row draws.
	line.offset_left = 10.0
	line.offset_right = -10.0
	line.add_theme_constant_override("separation", 10)
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	button.add_child(line)

	var name_label := ShellTheme.label(name_text, ShellTheme.T_ROW, ink)
	name_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	line.add_child(name_label)

	# Always built, even when empty. The ghost row's reason changes with the layout,
	# and a label created only when there was something to say would leave the row
	# silent on the pass that acquired a reason.
	choice.note_label = ShellTheme.kicker("", ShellTheme.PAPER_SOFT)
	line.add_child(choice.note_label)

	choice.values = HBoxContainer.new()
	choice.values.add_theme_constant_override("separation", 0)
	choice.values.mouse_filter = Control.MOUSE_FILTER_IGNORE
	line.add_child(choice.values)
	choice.button = button

	column.add_child(button)
	column.add_child(ShellTheme.rule(false, true))
	_draw_values(choice)
	_apply_row_state(choice)


## A row is reachable exactly when pressing it would change something.
##
## The paddock's rule — a row that is reachable and does nothing is a worse answer
## than one the focus walk skips — applied to a row whose value set can change
## under it. Measured: with a personal best set forward and none in reverse,
## cycling Layout to reverse used to leave the Ghost row focusable with one value,
## so a thumb landed on it and Cross did nothing at all.
##
## Focus is moved off before the row stops accepting it. That cannot currently
## happen — only the Layout row's press can shrink the Ghost row's value set, and
## focus is on Layout at that moment — but a focus owner that quietly becomes
## unfocusable is a screen the pad falls out of, and the guard is three lines.
func _apply_row_state(choice: Choice) -> void:
	if choice.button == null:
		return
	var steps := choice.steps()
	if not steps and choice.button.has_focus() and _go != null:
		_go.grab_focus()
	choice.button.disabled = not steps
	choice.button.focus_mode = Control.FOCUS_ALL if steps else Control.FOCUS_NONE


## Redraw one row's value cell. Every token is drawn, the active one in ink and the
## rest dimmed, separated by the plate's middle dot — `forward · reverse`. A row
## that showed only its current value would give a driver no way to know it moves.
func _draw_values(choice: Choice) -> void:
	if choice.note_label != null:
		# `ShellTheme.kicker` uppercases at construction, so a text set afterwards has
		# to do the same or the row grows a lowercase note nothing else on the sheet
		# matches.
		choice.note_label.text = choice.note.to_upper()
		choice.note_label.visible = not choice.note.is_empty()
	for child: Node in choice.values.get_children():
		choice.values.remove_child(child)
		child.queue_free()
	for index: int in choice.labels.size():
		if index > 0:
			choice.values.add_child(ShellTheme.label(" · ", ShellTheme.T_ROW,
					ShellTheme.PAPER_SOFT))
		var active := index == choice.index
		var ink := ShellTheme.PAPER_INK if active and not choice.dim \
				else ShellTheme.PAPER_SOFT
		var weight := ShellTheme.Weight.SEMIBOLD if active and choice.chooses \
				else ShellTheme.Weight.REGULAR
		var cell := ShellTheme.label(choice.labels[index], ShellTheme.T_ROW, ink, weight)
		cell.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		choice.values.add_child(cell)


func _on_row_pressed(choice: Choice) -> void:
	choice.advance()
	_draw_values(choice)
	_refresh()


# --- the rows that read something ------------------------------------------------


## Practice's scheduled limit, asked rather than typed.
##
## `session.h`'s `scheduled_limit()` answers Practice with `SessionLimit::open()`,
## and `circuit.gd` calls `use_scheduled_limit()` — so "open" is what the session
## will actually run under. The row is a reported fact and not a choice: `--laps`
## exists as an argument, but a lap count posted from here would be this menu
## choosing a session length that the FIA figures in `race_rules.h` have an answer
## for, which is the thing `circuit.gd:718` refuses to do in as many words.
func _length_choice() -> Choice:
	var session := KartSession.new()
	session.set_type(KartSession.TYPE_PRACTICE)
	session.use_scheduled_limit()
	var kind := String(KartSession.limit_kind_name(session.get_limit_kind()))
	if kind == "open":
		return Choice.fixed("open — until you leave")
	return Choice.fixed("%s %s" % [_trim_float(session.get_limit_value()), kind])


## Always "defaults", because that is what gets posted: the form sets no `tune` and
## no `preset`, so the session runs on `KartTuning`'s sourced defaults.
##
## When the preset directory holds saved presets the row says so and says that
## choosing one is not built, rather than pretending the directory is empty. That
## is the paddock's rule for an unbuilt affordance — a menu that quietly omits
## something the disk clearly has reads as a game that lost it.
##
## The directory and the extension come from `TuningPanel`, which is the thing that
## writes them (F6 in the tuning overlay). Spelling them again here is how a screen
## ends up counting `.tune` files in a directory of `.tuning` ones — and CLAUDE.md
## and `tuning.sh`'s own usage text both already say `.tune` where the writer says
## `.tuning`, so that drift is not hypothetical.
func _preset_choice() -> Choice:
	var made := Choice.fixed("defaults")
	var count := 0
	# Guarded rather than caught: `DirAccess.get_files_at()` on a directory that is
	# not there returns an empty array **and** pushes an engine error, so an unused
	# preset directory would print a red line into every run's log. Same guard
	# `shell_probe.gd:_gd_files` uses, for the same reason.
	if DirAccess.dir_exists_absolute(TuningPanel.PRESET_DIR):
		for entry: String in DirAccess.get_files_at(TuningPanel.PRESET_DIR):
			if entry.ends_with(TuningPanel.PRESET_EXTENSION):
				count += 1
	if count > 0:
		made.note = "%d saved in %s — choosing one is not built" % [
			count, TuningPanel.PRESET_DIR,
		]
	return made


## An assist, read from `KartSettings` and dimmed. The Settings screen owns these;
## this sheet reports them so the entry form records what the session was run
## under, which is what a real one does.
func _assist_choice(clutch: bool) -> Choice:
	var settings: KartSettings = shell.settings() if shell != null else null
	var made := Choice.fixed("on")
	made.dim = true
	if settings == null:
		made.labels = PackedStringArray(["unknown"])
		made.keys = made.labels
		made.note = "settings not loaded"
		return made
	var on: bool = settings.is_auto_clutch() if clutch else settings.is_auto_shift()
	made.labels = PackedStringArray(["on" if on else "off"])
	made.keys = made.labels
	return made


## The ghost row, and it is allowed to offer "best" only when there is one.
##
## `KartProfile.best_ghost_id()` answers for this exact track, layout and class, so
## the row is rebuilt against the layout the form currently has — a personal best
## set forward is not a ghost to drive against in reverse. When the profile has
## nothing, or the file it names is gone, the row is "off" with the reason.
##
## **`ghost=<id>` is posted and `circuit.gd` reads no such argument today.** The key
## is in `shell_probe.gd`'s `CIRCUIT_ARGS` and in `session_request.gd`'s header, so
## the project has already declared it belongs to that scene; the scene has not
## implemented its own declared argument, and until it does this row configures
## nothing. Reported rather than worked around.
func _ghost_choice() -> Choice:
	var made := Choice.fixed("off")
	made.chooses = true
	var profile: KartProfile = shell.profile() if shell != null else null
	if profile == null:
		made.note = "no profile — the extension is not built"
		return made
	var id := String(profile.best_ghost_id(_slug(), _layout_index(), KartSession.CLASS_KZ2))
	if id.is_empty():
		made.note = "no lap recorded here yet"
		return made
	var path := String(profile.best_ghost_path(_slug(), _layout_index(),
			KartSession.CLASS_KZ2))
	if not FileAccess.file_exists(path):
		made.note = "the recorded ghost is missing from disk"
		return made
	# "best" first, so the default on a profile that has one is to drive against it.
	made.labels = PackedStringArray(["best", "off"])
	made.keys = PackedStringArray([id, ""])
	made.note = ""
	return made


## The layout the form has selected, as `KartSession`'s enum. Derived from
## `KartTrack.is_reversed()` after selecting the chosen layout by name, so an
## authored layout whose name is neither "forward" nor "reverse" still resolves —
## ADR-0046 makes reverse an authored layout rather than a spline direction flag,
## and the names are the file's.
func _layout_index() -> int:
	if _track == null:
		return KartSession.LAYOUT_FORWARD
	_track.select_layout(_layout.key())
	return KartSession.LAYOUT_REVERSE if _track.is_reversed() \
			else KartSession.LAYOUT_FORWARD


# --- the map column ---------------------------------------------------------------


func _map_column() -> Control:
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	column.alignment = BoxContainer.ALIGNMENT_CENTER

	_map = TrackMap.new()
	_map.custom_minimum_size = Vector2(MAP_WIDTH, MAP_HEIGHT)
	_map.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	_map.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_map.set_ink(ShellTheme.PAPER_INK)
	if _track != null:
		_map.set_centerline(_track.centerline(MAP_SAGITTA, MAP_MAX_SPACING))
	column.add_child(_map)

	var caption := ShellTheme.kicker(_caption(), ShellTheme.PAPER_SOFT)
	caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(caption)
	return _sheet_pad(column)


## `1,375 m · 8 corners · 12.5 m elevation — drawn from track.json`, every figure
## measured off the loaded circuit.
##
## The elevation figure is `measurements()["elevation_range"]`, which is high minus
## low over a 0.05 m walk of the whole lap — not the difference between the two
## authored control points nearest the extremes, which a cubic Hermite profile
## overshoots between.
func _caption() -> String:
	if _track == null:
		return "no circuit loaded"
	var measured: Dictionary = _track.measurements()
	return "%s m · %d corners · %.1f m elevation — drawn from track.json" % [
		_thousands(int(roundf(_track.length()))),
		_track.corner_count(),
		float(measured["elevation_range"]),
	]


# --- footer -----------------------------------------------------------------------


func _footer() -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 20)

	_go = ShellTheme.row_button("Go to track", ShellTheme.PAPER_INK, ShellTheme.T_ROW)
	_go.pressed.connect(_on_go)
	_go.disabled = _track == null
	if _track == null:
		_go.focus_mode = Control.FOCUS_NONE
	row.add_child(_go)

	var gap := Control.new()
	gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(gap)

	_stamp = ShellTheme.kicker("", ShellTheme.PAPER_SOFT, ShellTheme.T_FOOT)
	row.add_child(_stamp)

	var pad := MarginContainer.new()
	pad.add_theme_constant_override("margin_left", 22)
	pad.add_theme_constant_override("margin_right", 22)
	pad.add_theme_constant_override("margin_top", 11)
	pad.add_theme_constant_override("margin_bottom", 11)
	pad.add_child(row)
	return pad


## The plate's `assists on · tuning at defaults`, assembled from what the two
## assist rows actually read. Mixed assists are named individually rather than
## averaged into one word: "assists on" over one assist that is off would be the
## stamp lying about the session it is stamping.
func _assist_stamp() -> String:
	var settings: KartSettings = shell.settings() if shell != null else null
	if settings == null:
		return "assists unknown · tuning at defaults"
	var clutch := settings.is_auto_clutch()
	var shift := settings.is_auto_shift()
	if clutch and shift:
		return "assists on · tuning at defaults"
	if not clutch and not shift:
		return "assists off · tuning at defaults"
	return "auto clutch %s · auto shift %s · tuning at defaults" % [
		"on" if clutch else "off", "on" if shift else "off",
	]


# --- state ------------------------------------------------------------------------


## Re-read everything a row change can move, and re-run the refusal.
##
## The ghost row is rebuilt because it is a function of the layout: `best_ghost_id`
## is keyed on track, layout and class, so switching to reverse can turn a row that
## said "best" into one that says "no lap recorded here yet". Rebuilding the value
## cell rather than only its highlight is what keeps that honest.
func _refresh() -> void:
	if _track == null:
		return
	var rebuilt := _ghost_choice()
	_ghost.labels = rebuilt.labels
	_ghost.keys = rebuilt.keys
	_ghost.note = rebuilt.note
	_ghost.index = mini(_ghost.index, maxi(0, _ghost.keys.size() - 1))
	_draw_values(_ghost)
	_apply_row_state(_ghost)

	if _stamp != null:
		_stamp.text = _assist_stamp().to_upper()

	var problem := refusal()
	if _refusal != null:
		_refusal.text = problem
	if _go != null:
		_go.disabled = not problem.is_empty()


## The `KartSession` this form would start, built the way `circuit.gd:690` builds
## it so the two agree field for field. Public so a probe can measure the refusal
## without driving the screen.
func session() -> KartSession:
	if _track == null:
		return null
	var made := KartSession.new()
	made.set_track(_slug())
	made.set_track_hash_hex(_track.content_hash())
	made.set_layout(_layout_index())
	made.set_condition(KartSession.CONDITION_DRY)
	made.set_type(KartSession.TYPE_PRACTICE)
	made.set_kart_class(KartSession.CLASS_KZ2)
	made.use_scheduled_limit()
	# One entry, and it is not a placeholder. `session.h`: *"1 is a legal value and
	# is what Practice uses"* — a field of one is the player alone rather than a
	# special case in the runner.
	made.set_entry_count(1)
	var settings: KartSettings = shell.settings() if shell != null else null
	if settings != null:
		made.set_auto_clutch(settings.is_auto_clutch())
		made.set_auto_shift(settings.is_auto_shift())
	made.set_tick_hz(Engine.physics_ticks_per_second)
	return made


## Why this form cannot start a session, or "".
##
## The four clauses below are `SessionRunner._first_problem()`'s, in its order and
## **in its words**. Deliberately duplicated rather than shared: the runner cannot
## be asked without a kart, a driver and a course, none of which exist in the
## shell — `shell_probe.gd` check 9b asserts as much — so the shared thing is
## `is_valid()`, which `src/core/session.h` says exists for exactly this pair of
## callers, and the three extra clauses are re-stated so a driver reads the same
## sentence here that a terminal would print there.
##
## Takes the session to judge, so the predicate can be measured against a state
## the form cannot currently produce. Called with nothing, it judges the form.
func refusal(made: KartSession = null) -> String:
	if made == null:
		if _track == null:
			return "no circuit loaded"
		made = session()
	if not made.is_valid():
		return "the session configuration is not valid — see KartSession.is_valid()"
	if made.get_kart_class() != KartSession.CLASS_KZ2:
		return "class %s is not simulated — nothing in src/ models it" % (
			KartSession.kart_class_name(made.get_kart_class())
		)
	if made.get_tick_hz() != Engine.physics_ticks_per_second:
		return "the session claims %d Hz and the engine runs %d" % [
			made.get_tick_hz(), Engine.physics_ticks_per_second,
		]
	if made.get_entry_count() > 1:
		return "an entry list of %d needs a field, and there is none yet" % (
			made.get_entry_count()
		)
	return ""


## What `Go to track` posts. Only keys the form set: `SessionRequest.take()` merges
## the command line **over** this, so an unset key is what leaves a recorded
## `shoot.sh` or `drive.sh` invocation meaning what it meant.
func config() -> Dictionary:
	var out := {
		"track": TRACK_PATH,
		"layout": _layout.key(),
		"session": SESSION_TYPE,
	}
	if not _ghost.key().is_empty() and _ghost.keys.size() > 1:
		out["ghost"] = _ghost.key()
	return out


func _on_go() -> void:
	var problem := refusal()
	if not problem.is_empty():
		# Should be unreachable — `_refresh()` disables the button — but a refusal
		# that appeared between the two would otherwise start a session the runner
		# kills on its first tick, and the driver would be shown a loading screen
		# for it.
		if _refusal != null:
			_refusal.text = problem
		return
	if shell != null:
		shell.start_session(config())


# --- focus and helpers -------------------------------------------------------------


## The first value row, or `Go to track` when nothing on the sheet can be changed.
## Never null while the form is built, because a screen with focusable controls and
## no entry point is one the pad cannot use and the stack only warns about it.
func initial_focus() -> Control:
	if _first_focus != null:
		return _first_focus
	return _go


## `1375` -> `1,375`. The plate groups its thousands and GDScript's `%d` does not.
static func _thousands(value: int) -> String:
	var digits := str(absi(value))
	var out := ""
	var placed := 0
	for index: int in range(digits.length() - 1, -1, -1):
		out = digits[index] + out
		placed += 1
		if placed % 3 == 0 and index > 0:
			out = "," + out
	return ("-" + out) if value < 0 else out


## A limit value with no trailing zeros: 3 laps rather than 3.0 laps. `%s` on a
## float prints `3` for a whole number in GDScript, which is the wanted behavior
## and is written down here because it is not obvious enough to rely on silently.
static func _trim_float(value: float) -> String:
	if is_equal_approx(value, roundf(value)):
		return str(int(roundf(value)))
	return "%.1f" % value
