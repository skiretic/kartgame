extends ShellScreen

## Plate 5. The main menu, and the bottom of the stack.
##
## Two paper cards over the 3D backdrop — "where you go" and "where you are" —
## because ADR-0052 decided modes have no screen of their own: **modes *are* the
## paddock**. So this is not a launcher in front of the game, it is the place the
## game is played from, and the right-hand card is what makes it a place rather
## than a list.
##
## The unbuilt modes are shown, disabled, with the reason in words: "needs a
## field — M7", "needs two circuits — M5". That is ADR-0052's rule for an unbuilt
## mode and it is the opposite of hiding them — a menu that quietly omits Career
## reads as a game that has no career, and a row that says why reads as a game
## being built. Rows whose *screen* is not built are omitted entirely, which is
## the honest version of the same rule one level down.

## The reason a mode is not selectable yet, verbatim from the plate. Present in
## this table means "designed, scheduled, not reachable"; absent means "works".
const NOT_YET := {
	"weekend": "needs a field — M7",
	"career": "needs two circuits — M5",
}

var _first_row: Button


func can_pop() -> bool:
	return false


func build() -> void:
	# No ground of its own. The paddock's whole point is that the backdrop is
	# visible behind it, so the screen paints nothing full-bleed and the two cards
	# carry the plate's drop shadow instead.
	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	for side: String in ["left", "right"]:
		margin.add_theme_constant_override("margin_" + side, 64)
	for side: String in ["top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 56)
	add_child(margin)

	var columns := HBoxContainer.new()
	columns.alignment = BoxContainer.ALIGNMENT_CENTER
	columns.add_theme_constant_override("separation", 24)
	margin.add_child(columns)

	columns.add_child(_where_you_go())
	columns.add_child(_where_you_are())


# --- left card ----------------------------------------------------------------


func _where_you_go() -> Control:
	var card := ShellTheme.panel(true, true)
	card.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	card.custom_minimum_size = Vector2(300.0, 0.0)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 6)
	card.add_child(column)

	column.add_child(ShellTheme.kicker("where you go", ShellTheme.PAPER_SOFT))
	column.add_child(_spacer(6))

	_add_mode(column, "Practice", "practice", "setup")
	_add_mode(column, "Race weekend", "weekend", "")
	_add_mode(column, "Career", "career", "")

	column.add_child(_spacer(8))
	column.add_child(ShellTheme.rule(false, true))
	column.add_child(_spacer(4))

	_add_row(column, "Profile", "profile")
	_add_row(column, "Settings", "settings")

	var quit := ShellTheme.row_button("Quit", ShellTheme.PAPER_INK)
	quit.pressed.connect(func() -> void: get_tree().quit())
	column.add_child(quit)
	if _first_row == null:
		_first_row = quit
	return card


## A mode row. Disabled with its reason when the mode is scheduled but unbuilt,
## and omitted outright when the *screen* it would open does not exist — an
## absent row is more honest than a button that does nothing, and it is what
## keeps this file correct while the screens land in parallel.
func _add_mode(column: VBoxContainer, text: String, key: String, screen: String) -> void:
	var reason := String(NOT_YET.get(key, ""))
	if reason.is_empty() and (screen.is_empty() or not shell.has_screen(screen)):
		return

	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)

	var button := ShellTheme.row_button(text, ShellTheme.PAPER_INK, ShellTheme.T_ROW)
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	if reason.is_empty():
		button.pressed.connect(func() -> void: shell.open(screen))
		if _first_row == null:
			_first_row = button
	else:
		button.disabled = true
		# Not focusable when it cannot be pressed. Check 5 walks the FOCUS_ALL set
		# and demands every member be reachable; a row that is reachable and does
		# nothing is a worse answer than a row the walk skips.
		button.focus_mode = Control.FOCUS_NONE
		row.add_child(button)
		row.add_child(ShellTheme.kicker(reason, ShellTheme.PAPER_SOFT))
		column.add_child(row)
		return

	row.add_child(button)
	column.add_child(row)


func _add_row(column: VBoxContainer, text: String, screen: String) -> void:
	if not shell.has_screen(screen):
		return
	var button := ShellTheme.row_button(text, ShellTheme.PAPER_INK)
	button.pressed.connect(func() -> void: shell.open(screen))
	column.add_child(button)
	if _first_row == null:
		_first_row = button


# --- right card ---------------------------------------------------------------


## Where you are: the career state, or the first-run invitation.
##
## The italic invitation lines are the fiction speaking, per ADR-0052, and italic
## is allowed here **and nowhere a number lives**. Liberation Sans ships no
## italic face and synthesizing one would slant the digits too, so the distinction
## is carried by color and weight instead — which is the same decision the plate's
## own `font-style: italic` was standing in for.
func _where_you_are() -> Control:
	var card := ShellTheme.panel(true, true)
	card.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	card.custom_minimum_size = Vector2(340.0, 0.0)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 6)
	card.add_child(column)

	column.add_child(ShellTheme.kicker("where you are", ShellTheme.PAPER_SOFT))
	column.add_child(_spacer(6))

	for line: String in _state_lines():
		var label := ShellTheme.label(line, ShellTheme.T_BODY, ShellTheme.PAPER_INK)
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.custom_minimum_size = Vector2(300.0, 0.0)
		column.add_child(label)
	return card


func _state_lines() -> PackedStringArray:
	var profile: KartProfile = shell.profile() if shell != null else null
	if profile == null:
		return PackedStringArray([
			"No profile — the extension is not built, so nothing can be saved yet.",
		])
	if profile.best_count() == 0:
		# ADR-0044 rule 1: two authored sentences, not one assembled from a
		# conditional fragment.
		return PackedStringArray([
			"No entry filed — the OK season starts when you do.",
			"No best set at Valdirone Nuova. The timing sheet is blank on purpose.",
		])
	var lines := PackedStringArray([
		"%s, %d best%s on file." % [
			profile.get_driver_name(), profile.best_count(),
			"" if profile.best_count() == 1 else "s",
		],
	])
	for index: int in mini(profile.best_count(), 3):
		var best: Dictionary = profile.best_at(index)
		lines.append("%s %s %s   %s" % [
			best.get("track_id", "?"), best.get("layout", "?"),
			best.get("kart_class", "?"),
			SessionRunner.format_time(float(best.get("lap_time_s", -1.0))),
		])
	return lines


static func _spacer(height: int) -> Control:
	var node := Control.new()
	node.custom_minimum_size = Vector2(0.0, float(height))
	return node


func initial_focus() -> Control:
	return _first_row


func title() -> String:
	return "paddock"
