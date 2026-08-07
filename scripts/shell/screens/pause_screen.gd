extends ShellScreen

## Plate 8. The screen-document family, as an overlay: a menu and the consequence
## of using it.
##
## ## This is the only door out of a Practice session
##
## `GAMEDESIGN.md` §4 gives Practice no limit — it is `LIMIT_OPEN` — so nothing
## inside the simulation can end one and `SessionRunner.end_session()` is its only
## ending. Without this screen the results sheet is unreachable from the mode
## anybody actually drives. It is not polish.
##
## ## The kart is not frozen, and the screen says so
##
## `get_tree().paused` is deliberately not used. `circuit.gd:790`'s `set_paused()`
## gates input at the driver through `SessionRunner.set_input_suspended()`, whose
## docstring at `session_runner.gd:458` named this caller in advance, and the
## world keeps running underneath — which is why the veil is
## `ShellTheme.VEIL_ALPHA` over the live frame rather than an opaque panel.
##
## **The session clock keeps running**, because `_tick_running()` keeps ticking.
## That is not a bug to hide behind a spinner; it is the fact that makes the
## consequence line necessary, and ADR-0052 §13 falls out of it for free.
##
## ## The consequence line is two authored sentences
##
## ADR-0044 rule 1, and it is not negotiable: one *whole* sentence per setting
## value, never one sentence with a conditional fragment spliced into it. The two
## are `CONSEQUENCE_STRUCK` and `CONSEQUENCE_FLAGGED` below and they are chosen by
## `pause_invalidates_lap`, which is a real saved setting and not a constant.
##
## ## Who builds this screen
##
## Nothing yet, and that is the wiring gap this file cannot close from inside
## itself. The shell is not in the tree during a session — `start_session()` swaps
## the whole scene — so a host inside `circuit.tscn` has to own a `ScreenStack` and
## push this. `bind_session()` is the entire interface it needs: a node with
## `set_paused(bool)` and `leave_session(String)`, which `circuit.gd` already has,
## plus optionally the configuration the session was started from so that
## "Restart session" has something to restart. Opened without one — from the
## paddock, from `--screen=pause`, from the gate — every row that needs a session
## is disabled *with its reason in words*, which is `paddock_screen.gd`'s rule for
## an unbuilt mode applied one level down.

## The two authored sentences. Whole sentences, one per setting value.
const CONSEQUENCE_STRUCK := (
	"The lap in progress will be struck out. The clock does not stop while this "
	+ "menu is open, so the time it is accumulating is not a time you drove."
)
const CONSEQUENCE_FLAGGED := (
	"The lap continues and the clock keeps running. A best set from here is "
	+ "recorded and flagged on the sheet, so the record says it was paused."
)

## What is shown when the setting cannot be read at all — a stale extension build,
## or `KartSettings` without the field. A third whole sentence rather than one of
## the two above with a shrug in front of it: claiming either outcome when neither
## is known would be the screen lying about the one thing it exists to say.
const CONSEQUENCE_UNKNOWN := (
	"This build cannot read the pause-invalidation setting, so what happens to "
	+ "the lap is unknown. Rebuild the extension before trusting a time set after "
	+ "this menu."
)

## Why a row is not selectable, verbatim on the screen next to it.
const NO_SESSION := "needs a session"
const NO_CONFIG := "the session did not hand over its configuration"

var _host: Node
var _config := {}
var _own_settings: KartSettings
var _rows: VBoxContainer
var _first: Button
var _consequence: Label
var _detail: VBoxContainer
var _showing_controls := false


# --- the host's interface ---------------------------------------------------------


## Bind the running session. `host` must expose `set_paused(bool)` and
## `leave_session(String)`; `config` is what the session was started from, in
## `circuit.gd`'s argument vocabulary, and is what makes "Restart session"
## possible.
##
## Checked by name and refused loudly, because a host that is silently not a host
## produces a Resume button that resumes nothing — and the kart is still driving.
func bind_session(host: Node, config := {}) -> void:
	if host != null:
		var missing := PackedStringArray()
		for method: String in ["set_paused", "leave_session"]:
			if not host.has_method(method):
				missing.append(method)
		if not missing.is_empty():
			push_error("%s cannot host the pause menu: no %s()"
					% [host.get_class(), ", ".join(missing)])
			return
	_host = host
	_config = config.duplicate()
	if _rows != null:
		_rebuild_rows()
	_refresh()


## The kart is driving underneath and its input is gated at the driver. Called on
## entry and on exit rather than from `bind_session`, so a host that binds before
## pushing does not gate the input a frame early.
func on_enter() -> void:
	_set_host_paused(true)


func on_exit() -> void:
	_set_host_paused(false)


## For a probe: which sentence is on the screen right now.
func consequence_text() -> String:
	return _consequence.text if _consequence != null else ""


# --- the setting ------------------------------------------------------------------


## **The one place `pause_invalidates_lap` is read.** If agent C's naming differs,
## this function is the whole edit.
##
## Called unconditionally and *not* behind `has_method()`. `Dictionary.get(key,
## default)` and its relatives hiding a renamed key forever is a documented trap in
## this project — it left all four wheels dead straight through every corner of
## every lap while the solver steered 25 degrees underneath them — and a
## `has_method()` guard on a contract method is the same silence with a different
## spelling.
##
## The return is `Variant` rather than `bool` for one measured reason: a call to a
## method that does not exist raises a script error and aborts **the function it is
## in**, returning null to its caller, which then continues. Measured in 4.7.1. So
## the failure is contained here and surfaces as `CONSEQUENCE_UNKNOWN` — loud in
## the log, loud on the screen, and never a silent default.
func _pause_invalidates_lap() -> Variant:
	var settings: KartSettings = _settings()
	if settings == null:
		return null
	return settings.is_pause_invalidates_lap()


## The shell's `KartSettings` where there is a shell, and one read off disk where
## there is not — during a session the shell is not in the tree at all, because
## `start_session()` swapped the whole scene.
##
## `_own_settings` is cached: `load()` touches the filesystem and this is read on
## every refresh.
func _settings() -> KartSettings:
	if shell != null:
		var shared: KartSettings = shell.settings()
		if shared != null:
			return shared
	if _own_settings != null:
		return _own_settings
	if not ClassDB.class_exists("KartSettings"):
		return null
	_own_settings = KartSettings.new()
	_own_settings.load()
	return _own_settings


# --- construction -------------------------------------------------------------------


func is_overlay() -> bool:
	return true


func build() -> void:
	# The veil, at the plate's own alpha. The world stays visible behind it because
	# the world is still running; an opaque panel would be the screen claiming a
	# pause the simulation does not have.
	var page := ScreenPanel.new(ShellTheme.VEIL_ALPHA)
	add_child(page)

	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 0)
	page.field().add_child(columns)

	var left := MarginContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.size_flags_vertical = Control.SIZE_EXPAND_FILL
	for side: String in ["left", "right"]:
		left.add_theme_constant_override("margin_" + side, 22)
	columns.add_child(left)

	var menu := VBoxContainer.new()
	menu.alignment = BoxContainer.ALIGNMENT_CENTER
	menu.add_theme_constant_override("separation", 6)
	left.add_child(menu)

	menu.add_child(ShellTheme.kicker("paused", ShellTheme.SCR_SOFT))
	_rows = VBoxContainer.new()
	_rows.add_theme_constant_override("separation", 6)
	menu.add_child(_rows)

	columns.add_child(ScreenPanel.divider())

	var right := MarginContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	for side: String in ["left", "right"]:
		right.add_theme_constant_override("margin_" + side, 22)
	columns.add_child(right)

	_detail = VBoxContainer.new()
	_detail.alignment = BoxContainer.ALIGNMENT_CENTER
	_detail.add_theme_constant_override("separation", 6)
	right.add_child(_detail)

	_rebuild_rows()
	_refresh()


## The four rows. Rebuilt rather than patched when a host binds, because whether a
## row is selectable at all depends on the host and a disabled button that later
## becomes enabled would have to be un-disabled, re-focused and re-labelled — three
## states where there is only one fact.
func _rebuild_rows() -> void:
	for child: Node in _rows.get_children():
		_rows.remove_child(child)
		child.queue_free()
	_first = null

	_add_row("Resume", _resume, "")
	_add_row("Restart session", _restart, _restart_reason())
	_add_row("Controls", _toggle_controls, "")
	_add_row("Quit to paddock", _quit, "" if _can_quit() else NO_SESSION)
	_chain_focus()


## Link the selectable rows top-to-bottom by hand.
##
## **Measured, not precautionary.** With the disabled "Restart session" row in the
## middle, `find_valid_focus_neighbor()`'s geometric walk reached Resume and
## Controls and left **Quit to paddock** unreachable — 2 of 3, and Quit is the only
## way out of a Practice session, so the one row a pad could not reach was the one
## that matters. The disabled row is an `HBoxContainer` carrying a button *and* its
## reason kicker, which is wider and taller than the plain rows around it, and the
## engine's walk loses the column across it.
##
## Explicit neighbors are exact and cost four properties. The ends are left unset
## on purpose: `ScreenStack._navigate()` falls through to `_wrap()` when a
## neighbor is null, which is what makes Down at the bottom of a list land on its
## top instead of doing nothing.
func _chain_focus() -> void:
	var selectable: Array[Button] = []
	for row: Node in _rows.get_children():
		for child: Node in row.get_children():
			var button := child as Button
			if button != null and button.focus_mode == Control.FOCUS_ALL:
				selectable.append(button)
	for index: int in selectable.size() - 1:
		var above := selectable[index]
		var below := selectable[index + 1]
		above.focus_neighbor_bottom = above.get_path_to(below)
		below.focus_neighbor_top = below.get_path_to(above)
		# `focus_next`/`focus_previous` are Tab's path rather than the d-pad's, and
		# a keyboard that walked a different order than the pad would be two menus.
		above.focus_next = above.get_path_to(below)
		below.focus_previous = below.get_path_to(above)


## One menu row, disabled with its reason in words when it cannot be used.
##
## A disabled row is `FOCUS_NONE`, which is `paddock_screen.gd`'s decision and its
## reasoning holds exactly: `shell_probe.gd` check 5 demands every focusable be
## reachable, and a row that is reachable and does nothing is a worse answer than a
## row the walk skips.
func _add_row(text: String, action: Callable, reason: String) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)

	var button := ShellTheme.row_button(text, ShellTheme.SCR_INK, ShellTheme.T_ROW)
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	if reason.is_empty():
		button.pressed.connect(action)
		if _first == null:
			_first = button
	else:
		button.disabled = true
		button.focus_mode = Control.FOCUS_NONE
		row.add_child(button)
		row.add_child(ShellTheme.kicker(reason, ShellTheme.SCR_SOFT))
		_rows.add_child(row)
		return

	row.add_child(button)
	_rows.add_child(row)


# --- the right column ------------------------------------------------------------


func _refresh() -> void:
	if _detail == null:
		return
	for child: Node in _detail.get_children():
		_detail.remove_child(child)
		child.queue_free()
	_consequence = null

	if _showing_controls:
		_build_controls()
		return
	_build_consequence()


## Which whole sentence goes with which setting value. Pure, so the mapping can be
## measured without a `KartSettings` and without a running session — which is the
## only way to check it at all while the setting itself is being added elsewhere.
##
## `true` is the default and the strict answer; `false` is the permissive one;
## anything that is not a `bool` — which is what `_pause_invalidates_lap()` returns
## when the setting cannot be read — is neither, and says so.
static func consequence_for(invalidates: Variant) -> String:
	if not invalidates is bool:
		return CONSEQUENCE_UNKNOWN
	return CONSEQUENCE_STRUCK if bool(invalidates) else CONSEQUENCE_FLAGGED


## The plate's right column: the two facts about what pause is, then the sentence
## about what it costs.
func _build_consequence() -> void:
	_detail.add_child(ShellTheme.kicker("the kart is not frozen", ShellTheme.SCR_SOFT))
	_detail.add_child(_line("Input is gated at the driver. The world, the clock and "
			+ "the physics keep running."))

	var invalidates: Variant = _pause_invalidates_lap()
	_consequence = _line(consequence_for(invalidates))
	_consequence.add_theme_color_override("font_color",
			ShellTheme.SCR_SOFT if invalidates is bool and not bool(invalidates)
			else ShellTheme.ST_SLOWER)
	_detail.add_child(_consequence)

	var state := "unreadable"
	if invalidates is bool:
		state = "on" if bool(invalidates) else "off"
	_detail.add_child(ShellTheme.kicker(
			"pause invalidation: %s · change it with the assists" % state,
			ShellTheme.SCR_SOFT))


## The real bindings for whatever is plugged in, from `ControlHints` — the same
## list the running game prints, and the only reader-checked copy there is. A
## second hand-kept list here is precisely how the printed one came to name F11 and
## F12 for keys bound to F3 and F5.
func _build_controls() -> void:
	_detail.add_child(ShellTheme.kicker("controls", ShellTheme.SCR_SOFT))
	for text: String in ControlHints.lines():
		_detail.add_child(_line(text))
	_detail.add_child(ShellTheme.kicker("press controls again to go back",
			ShellTheme.SCR_SOFT))


func _line(text: String) -> Label:
	var node := ShellTheme.label(text, ShellTheme.T_BODY, ShellTheme.SCR_INK)
	node.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	node.custom_minimum_size = Vector2(360.0, 0.0)
	return node


# --- the rows' actions ---------------------------------------------------------------


## Resume is `menu_back`, spelled as a row. `ScreenStack.back()` is public for
## exactly this: Circle and the Resume row must be one act, not two code paths that
## agree today.
func _resume() -> void:
	if stack != null:
		stack.call("back")


## Re-post the configuration and reload the scene.
##
## `SessionRequest.take()` is destructive, so the running scene has already
## drained what it was started from — which is why the configuration comes in
## through `bind_session()` rather than being read back out of anything. With no
## configuration the row is disabled and says so; it does not reload into a scene
## with no track.
func _restart() -> void:
	if _config.is_empty():
		return
	SessionRequest.post(_config)
	get_tree().reload_current_scene()


func _restart_reason() -> String:
	if _host == null:
		return NO_SESSION
	if _config.is_empty():
		return NO_CONFIG
	return ""


func _toggle_controls() -> void:
	_showing_controls = not _showing_controls
	_refresh()


## Out. `leave_session()` ends the session through the runner, so the result
## reaches the shell the same way the flag falling does, and the results sheet
## opens on the way in. Without a host there is nothing to end — the shell path is
## a plain `reset_to`.
func _quit() -> void:
	if _host != null:
		_host.call("leave_session", "left for the paddock")
		return
	if shell != null:
		shell.reset_to("paddock")


func _can_quit() -> bool:
	return _host != null or shell != null


func _set_host_paused(paused: bool) -> void:
	if _host != null:
		_host.call("set_paused", paused)


# --- ShellScreen ----------------------------------------------------------------------


func initial_focus() -> Control:
	return _first


func title() -> String:
	return "pause"
