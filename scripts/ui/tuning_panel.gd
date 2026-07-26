class_name TuningPanel
extends CanvasLayer

## The tuning overlay. `src/core/tuning.h`'s vocabulary and `src/tuning/`'s
## `KartTuning` node, made turnable **while the kart is moving**. ROADMAP M3b's
## last bullet, issue #159.
##
## ## Why it has to work at speed, which decides almost everything below
##
## Twelve of this project's constants can only be set by driving or by listening.
## Judging one means changing it and feeling the difference *in the same corner* —
## a knob that pauses the game, eats the driving inputs or costs a menu round trip
## is a knob that gets turned once, between laps, against a memory of how the last
## lap felt. So: nothing here pauses, nothing here blocks, the overlay is hidden by
## default, and every control is a single key that does its thing on the frame it
## is pressed.
##
## It replaces `test_track.gd`'s `[` / `]` prototype, and inherits that prototype's
## one real rule: **the knob is on the HUD, not in a secret keybinding.** This
## project once shipped a milestone where the shift, clutch and look-back keys
## existed and were documented nowhere, and the result was a driver pressing E,
## getting nothing, and concluding the gearbox was broken. The footer of this panel
## lists every key it answers to.
##
## ## It is an audit trail that happens to be adjustable
##
## `ARCHITECTURE.md` §19 names unbounded vehicle tuning as the live risk, and
## ADR-0033 refused to retune M3a's constants rather than restore a pretty number
## by moving one. A sliders-and-a-save-button overlay would be that risk shipped.
## `core/tuning.h`'s header makes the full argument; what it means *here* is that
## the citation is not a footnote on this panel, it is the largest block on it. A
## person about to turn a knob reads why the number is what it is **before** they
## turn it, and a defended default — `Sourced` or `Measured` — reads as defended
## before it is touched rather than after.
##
## ## Nothing on this panel is a second copy of the table
##
## Every label, unit, range, step, provenance, home and citation comes from
## `KartTuning.descriptors()`, which serves `core/tuning.h`'s `TUNABLES` verbatim.
## `ARCHITECTURE.md` §5 item 10 applied to a UI: a second owner of a justification
## is how justifications rot, and a hardcoded row here would be a copy of a
## citation that nothing keeps in step.
##
## ## Adding it to a scene
##
##     TuningPanel.attach(self)
##
## That is the whole integration, and it mirrors `EngineVoiceRig.attach`. The panel
## then finds its registry the way `telemetry_panel.gd` finds its source: the first
## node in the `tuning_registry` group that can answer `descriptors()`. A scene that
## has no `KartTuning` gets a panel that says so and changes nothing.
##
## ## Colorblind safety, §18
##
## The four provenance classes are never distinguished by hue alone. Each row
## carries a bracketed letter — `[S]` `[M]` `[D]` `[U]` — the class name is spelled
## out in the detail block below the list, and a defended row carries the word
## `LOCK` or `OPEN` rather than a colored dot. Run the panel through a monochrome
## filter and every class is still readable.

# --- wiring ------------------------------------------------------------------

## The group `KartTuning::_ready` joins, exactly as `KartBody` joins
## `telemetry_source`. Duck-typed on `descriptors()` rather than on the class, so
## the panel loads and draws its "no registry" message when the GDExtension is not
## registered at all — which is every `tests/run.sh` run and every headless gate.
const REGISTRY_GROUP := &"tuning_source"

## Where a preset goes. `KartTuning.save_preset` creates the directory.
const PRESET_DIR := "user://tuning"
const PRESET_EXTENSION := ".tuning"

# --- input -------------------------------------------------------------------

## Auto-repeat: how long a key is held before it starts repeating, and how fast it
## repeats after that.
##
## 0.35 s and 14 Hz, chosen against the widest range in the table rather than by
## feel. `steer_rate` spans 1.0 to 12.0 in steps of 0.1, which is 110 presses end
## to end; at 14 Hz that is 7.9 s of holding a key, which is a long hold and not a
## hopeless one. The delay has to be longer than a deliberate single press — a
## press that repeated once would move a value two steps and look like a bug — and
## short enough that a driver who wants to sweep does not think the key is dead.
## The rate is the other half of the same trade: faster than about 20 Hz and a
## single step stops being individually audible, which matters because half these
## constants are judged by ear.
const REPEAT_DELAY := 0.35
const REPEAT_INTERVAL := 1.0 / 14.0

# --- look --------------------------------------------------------------------

## Okabe-Ito, the same colorblind-safe eight `telemetry_panel.gd` uses. Kept in
## that file's order and hex so the two overlays read as one project.
##
## The provenance colors are picked so that **vermillion means one thing**: it is
## the arm-to-override warning and nothing else. An `Unsourced` default is not an
## alarm — it is the class this whole system exists to let somebody turn freely —
## so it gets reddish purple and the alert color stays spent on the one gesture
## that can destroy a real number.
const COLOR_SOURCED := Color(0.000, 0.447, 0.698) # 0072B2 blue
const COLOR_MEASURED := Color(0.000, 0.620, 0.451) # 009E73 bluish green
const COLOR_DERIVED := Color(0.941, 0.894, 0.259) # F0E442 yellow
const COLOR_UNSOURCED := Color(0.800, 0.475, 0.655) # CC79A7 reddish purple
const COLOR_ALERT := Color(0.835, 0.369, 0.000) # D55E00 vermillion

const COLOR_PANEL := Color(0.04, 0.05, 0.07, 0.88)
const COLOR_ROW := Color(1.0, 1.0, 1.0, 0.05)
const COLOR_SELECTED := Color(1.0, 1.0, 1.0, 0.16)
const COLOR_RULE := Color(1.0, 1.0, 1.0, 0.12)
const COLOR_TEXT := Color(0.88, 0.90, 0.94)
const COLOR_DIM := Color(0.60, 0.63, 0.70)
const COLOR_CHANGED := Color(0.941, 0.894, 0.259) # F0E442, same as Derived
const COLOR_GOOD := Color(0.000, 0.620, 0.451)

const FONT_SIZE_SMALL := 12
const FONT_SIZE_BODY := 14
const FONT_SIZE_TITLE := 17

const PANEL_W := 786.0
const PAD := 14.0
const ROW_H := 22.0
const HEADER_H := 62.0
const DETAIL_H := 158.0
const FOOTER_H := 26.0
const MARGIN := 16.0

## How much of the viewport the list may take before it starts scrolling. The road
## ahead has to stay visible — a tuning overlay that has to be closed to drive gets
## closed, which is `telemetry_panel.gd`'s argument for its own layout.
const MAX_HEIGHT_FRACTION := 0.72

## Rows kept between the selection and the edge of the list while scrolling, so the
## next row down is already on screen when the caret reaches it.
const SCROLL_MARGIN_ROWS := 2

## How long a save result stays on the footer, seconds. It is also printed, so this
## only has to be long enough to notice, not long enough to read twice.
const NOTICE_SECONDS := 6.0

# --- state -------------------------------------------------------------------

var _canvas: Control
var _font: Font

## The `KartTuning`. Never constructed here — the panel is a view.
var _registry: Node = null

## `descriptors()` allocates an Array of Dictionaries and the table is immutable,
## so it is read exactly once per registry and never in a draw or an input path.
var _rows: Array = []

var _selected := 0
var _scroll := 0

## The tunable armed for an override, or -1. See `_press_unlock`.
var _armed := -1

var _repeat_action := &""
var _repeat_timer := 0.0

var _notice := ""
var _notice_color := COLOR_TEXT
var _notice_until := 0.0

## Looked for twice a second rather than every frame, the same rate and for the
## same reason as `telemetry_panel.gd`: the group query allocates, and a registry
## that is not in the tree yet will not be there a hundredth of a second later.
var _search_countdown := 0


## Build the panel and add it to `parent`. The construction pattern is
## `EngineVoiceRig.attach`'s: one static call, no scene file, no exported path to
## wire, and the caller keeps the returned node only if it wants it.
static func attach(parent: Node) -> TuningPanel:
	if parent == null:
		return null
	var panel := TuningPanel.new()
	panel.name = "TuningPanel"
	parent.add_child(panel)
	return panel


func _ready() -> void:
	# Above the telemetry panel's 100, because this one is modal in attention even
	# though it is not modal in input: when it is open it is the thing being read.
	layer = 101

	# `driving_hud.gd` records the burn this guards. Under `--headless` there is no
	# rendering server and every `draw_rect` in `_draw` emits its own error with a
	# stack trace, which buries a scenario report under thousands of lines that look
	# like a solver fault. This panel starts hidden so it would normally draw
	# nothing — the guard is here so that an errant `tune_toggle` in a headless
	# harness cannot turn it on either.
	if DisplayServer.get_name() == "headless":
		set_process(false)
		set_process_unhandled_input(false)
		return

	_font = ThemeDB.fallback_font

	_canvas = Control.new()
	_canvas.name = "Canvas"
	_canvas.set_anchors_preset(Control.PRESET_FULL_RECT)
	# Never eats a click. The scene under this is being driven.
	_canvas.mouse_filter = Control.MOUSE_FILTER_IGNORE
	# The `draw` signal rather than a `_draw` override, so the whole overlay stays
	# in one file — `telemetry_panel.gd` does the same for the same reason.
	_canvas.draw.connect(_paint)
	_canvas.resized.connect(_canvas.queue_redraw)
	_canvas.visible = false
	add_child(_canvas)

	# Only `tune_toggle` is read while closed; everything else is routed through
	# `_unhandled_input`, which is switched on and off with the overlay. See
	# `_process` for why that is not quite enough on this project's input map.
	set_process_unhandled_input(false)


## Point the panel at a registry directly, for a scene that would rather wire it
## than join the group. Same shape as `TelemetryPanel.set_source`.
func set_registry(registry: Node) -> void:
	_registry = registry
	_rows = []
	if _registry != null and _registry.has_method(&"descriptors"):
		_adopt(_registry)


func is_open() -> bool:
	return _canvas != null and _canvas.visible


# --- run time ----------------------------------------------------------------


func _process(delta: float) -> void:
	# Read here rather than in `_unhandled_input` because it is the one action that
	# has to work while the overlay is closed, and F2 collides with nothing.
	if Input.is_action_just_pressed(&"tune_toggle"):
		_toggle()

	if not is_open():
		return

	if _registry == null or not is_instance_valid(_registry):
		_search_countdown -= 1
		if _search_countdown <= 0:
			_search_countdown = 30
			_find_registry()

	_tick_repeat(delta)

	# The only thing that changes without an input is the notice expiring, so the
	# panel is redrawn on events rather than every frame. A full re-record of a
	# canvas item costs real time — `telemetry_panel.gd` measured 3.1 ms for its
	# own, three times ARCHITECTURE.md §15's whole budget for game logic and UI —
	# and this overlay is open exactly when the frame time matters most, because
	# the thing being judged is how the kart feels.
	if _notice != "" and _seconds() >= _notice_until:
		_notice = ""
		_canvas.queue_redraw()


func _toggle() -> void:
	_canvas.visible = not _canvas.visible
	set_process_unhandled_input(_canvas.visible)
	# Closing disarms. An override armed before a lap and confirmed after it is not
	# a deliberate gesture, it is a keystroke that outlived its own warning.
	_armed = -1
	_stop_repeat()
	if _canvas.visible and _registry == null:
		_find_registry()
	_canvas.queue_redraw()


## The zero-integration path, and it is `telemetry_panel.gd`'s exactly: the first
## node in the group that can answer the method. Duck-typed rather than
## class-checked so this file does not fail to parse when the GDExtension is
## absent.
func _find_registry() -> void:
	for node in get_tree().get_nodes_in_group(REGISTRY_GROUP):
		if node.has_method(&"descriptors"):
			_adopt(node)
			return


func _adopt(registry: Node) -> void:
	_registry = registry
	# Read once. The table is immutable, `descriptors()` allocates an Array of
	# fifteen-key Dictionaries, and neither a draw nor a keypress may pay for that.
	_rows = registry.descriptors()
	_selected = clampi(_selected, 0, maxi(_rows.size() - 1, 0))
	_scroll = 0
	# A preset loaded by something else moves values this panel is displaying, and
	# it is the registry's own signal that says so. Guarded because the signal is
	# the boundary's, not this file's, and an overlay must not be the thing that
	# stops a scene loading.
	if registry.has_signal(&"tuning_changed") \
			and not registry.is_connected(&"tuning_changed", _on_tuning_changed):
		registry.connect(&"tuning_changed", _on_tuning_changed)
	# Null under `--headless`, where `_ready` builds no canvas at all.
	if _canvas != null:
		_canvas.queue_redraw()


func _on_tuning_changed(_key: String, _value: float, _owner: int) -> void:
	if is_open():
		_canvas.queue_redraw()


# --- input -------------------------------------------------------------------


## Every control except the toggle, consumed only while the overlay is open.
##
## **These are deliberately not the arrow keys, and the reason is worth keeping.**
## The obvious binding for a list is Up/Down/Left/Right, and it is wrong here:
## `project.godot` binds the arrows as the second half of `throttle`, `brake`,
## `steer_left` and `steer_right`, and `KartBody::gather_input` reads all four by
## **polling** `Input.get_action_strength` in `_physics_process` rather than
## through the event queue. `set_input_as_handled()` does not touch the `Input`
## singleton, so holding Right to sweep a value would have applied full right lock
## at the same time — an overlay that looked correct while it drove the kart into
## the grass.
##
## So the keyboard half is PageUp/PageDown to select and `[` / `]` to adjust, and
## the pad half is the d-pad, which is bound to nothing else. Marking the event
## handled still matters: it stops any other event-driven consumer from also
## seeing it.
func _unhandled_input(event: InputEvent) -> void:
	if not is_open():
		return

	if event.is_action_pressed(&"tune_prev"):
		_move(-1)
		_start_repeat(&"tune_prev")
	elif event.is_action_pressed(&"tune_next"):
		_move(1)
		_start_repeat(&"tune_next")
	elif event.is_action_pressed(&"tune_decrease"):
		_nudge(-1)
		_start_repeat(&"tune_decrease")
	elif event.is_action_pressed(&"tune_increase"):
		_nudge(1)
		_start_repeat(&"tune_increase")
	elif event.is_action_pressed(&"tune_reset"):
		_press_reset()
	elif event.is_action_pressed(&"tune_unlock"):
		_press_unlock()
	elif event.is_action_pressed(&"tune_save"):
		_press_save()
	else:
		return
	get_viewport().set_input_as_handled()


func _start_repeat(action: StringName) -> void:
	_repeat_action = action
	_repeat_timer = REPEAT_DELAY


func _stop_repeat() -> void:
	_repeat_action = &""
	_repeat_timer = 0.0


## Held keys, polled rather than driven off `echo`, because the OS key-repeat rate
## is a system preference and this one is a design decision — see `REPEAT_DELAY`.
## Polling also means the pad's d-pad repeats at the same rate as the keyboard,
## which `echo` would not give at all.
func _tick_repeat(delta: float) -> void:
	if _repeat_action == &"":
		return
	if not Input.is_action_pressed(_repeat_action):
		_stop_repeat()
		return
	_repeat_timer -= delta
	# A loop rather than a single step, so a frame-rate collapse does not silently
	# swallow presses the driver was owed.
	while _repeat_timer <= 0.0:
		_repeat_timer += REPEAT_INTERVAL
		match _repeat_action:
			&"tune_decrease": _nudge(-1)
			&"tune_increase": _nudge(1)
			&"tune_prev": _move(-1)
			&"tune_next": _move(1)


func _move(direction: int) -> void:
	if _rows.is_empty():
		return
	_selected = clampi(_selected + direction, 0, _rows.size() - 1)
	# Moving off an armed row disarms it. The warning names one tunable, so it may
	# only ever confirm that one.
	_armed = -1
	# The repeat is armed by the caller, not here: `_tick_repeat` calls this every
	# repeat, and re-arming from inside would reset the delay each time and turn a
	# held key into one step per REPEAT_DELAY forever.
	_canvas.queue_redraw()


func _nudge(steps: int) -> void:
	var row := _selected_row()
	if row.is_empty() or _registry == null:
		return
	var id := int(row["id"])
	var before: float = _registry.get_value(id)
	# The registry refuses a defended tunable that has not been acknowledged and
	# returns the unchanged value. Not re-derived here: `defended` is served in the
	# descriptor and the refusal is the registry's rule, so this only has to notice
	# that nothing moved and say why.
	var after: float = _registry.nudge(id, steps)
	if after == before and bool(row["defended"]) and not bool(_registry.is_acknowledged(id)):
		_say("%s is a %s default — press U twice to override it"
				% [row["key"], row["provenance_name"]], COLOR_ALERT)
	_canvas.queue_redraw()


func _press_reset() -> void:
	var row := _selected_row()
	if row.is_empty() or _registry == null:
		return
	# Never refused, and it must not be: putting a sourced number back where the
	# source put it is not an override, and a ceremony around it would teach a
	# driver that undoing a mistake is as expensive as making one.
	_registry.reset_value(int(row["id"]))
	_armed = -1
	_say("%s back to its default" % row["key"], COLOR_GOOD)
	_canvas.queue_redraw()


## The two-press override, and the reason it is two.
##
## A single key that silently unlocks a sourced constant is the exact failure this
## whole system exists to prevent: `ARCHITECTURE.md` §19's unbounded vehicle tuning
## arrives one convenient keystroke at a time. So the first press only **arms**,
## and the panel then spends its largest block saying which citation is about to be
## overridden; the second press calls `acknowledge`, which is the registry's own
## per-tunable, per-session unlock and prints the citation at warning level.
##
## Moving the selection, closing the overlay or resetting the row all disarm.
func _press_unlock() -> void:
	var row := _selected_row()
	if row.is_empty() or _registry == null:
		return
	var id := int(row["id"])
	if not bool(row["defended"]):
		_say("%s is a %s default — nothing to unlock, just turn it"
				% [row["key"], row["provenance_name"]], COLOR_DIM)
		return
	if bool(_registry.is_acknowledged(id)):
		# Offered so a slip can be undone without restarting, which is the registry
		# header's stated reason for `withdraw_acknowledgement` existing at all.
		_registry.withdraw_acknowledgement(id)
		_armed = -1
		_say("%s locked again" % row["key"], COLOR_GOOD)
	elif _armed != id:
		_armed = id
		_say("press U again to override %s" % row["key"], COLOR_ALERT)
	else:
		_registry.acknowledge(id)
		_armed = -1
		_say("%s UNLOCKED — a %s default is now movable" % [row["key"], row["provenance_name"]],
				COLOR_ALERT)
	_canvas.queue_redraw()


## Save a preset, named from the session rather than typed.
##
## There is no text entry on this overlay and there must not be: a field that takes
## the keyboard is a field that stops the kart being driven, and every one of these
## numbers is judged while it is moving. So the name is the scene plus a UTC
## timestamp — `test_track-2026-07-26T14-08-33` — which is unique, sorts
## chronologically, and says which of the two driving scenes the feel was judged in.
## That last part is not decoration: the proving ground is where every §6.4 figure
## is measured and the test track is where the corners are, and "which one was this
## tuned on" is a question a preset from three weeks ago has to be able to answer.
##
## Colons are stripped because they are not legal in a filename on every platform
## this runs on, and the same stem is passed as the preset's own `name` so the file
## and its header agree.
func _press_save() -> void:
	if _registry == null:
		_say("no tuning registry — nothing to save", COLOR_ALERT)
		return
	if bool(_registry.is_at_defaults()):
		# Saving an empty diff is legal and useless. Said rather than done, because
		# a driver who pressed F6 believes something happened.
		_say("everything is at its default — a preset would be empty", COLOR_DIM)
		return

	var scene := get_tree().current_scene
	var stem := "%s-%s" % [
		(scene.name if scene != null else "session").to_snake_case(),
		Time.get_datetime_string_from_system(true).replace(":", "-"),
	]
	var path := "%s/%s%s" % [PRESET_DIR, stem, PRESET_EXTENSION]
	var error: int = _registry.save_preset(path, stem)
	if error != OK:
		_say("could not write %s (%s)" % [path, error_string(error)], COLOR_ALERT)
		push_error("tuning: could not write %s (%s)" % [path, error_string(error)])
		return

	# Printed as well as shown, which is the rule `test_track.gd`'s prototype
	# established: a session that found a good number leaves a record in the
	# terminal and not only on a HUD that is gone. The absolute path is printed
	# because `user://` is somewhere different on every platform and a preset
	# nobody can find is a preset nobody reviews.
	_say("saved %s" % path, COLOR_GOOD)
	print("tuning: saved %s (%s), %d changed, %d defended override(s), %s"
			% [ProjectSettings.globalize_path(path), stem,
			int(_registry.changed_count()), int(_registry.defended_override_count()),
			String(_registry.tuning_hash_hex())])
	# The whole diff, so the terminal carries what the file carries. `audit_text()`
	# is the same body `tools/verify/tuning.sh` prints.
	print(String(_registry.audit_text()))


func _say(text: String, color: Color) -> void:
	_notice = text
	_notice_color = color
	_notice_until = _seconds() + NOTICE_SECONDS


func _seconds() -> float:
	return float(Time.get_ticks_msec()) / 1000.0


func _selected_row() -> Dictionary:
	if _selected < 0 or _selected >= _rows.size():
		return {}
	var row: Dictionary = _rows[_selected]
	return row


# --- drawing -----------------------------------------------------------------


func _paint() -> void:
	var frame := _canvas.size
	if frame.x < 1.0 or frame.y < 1.0:
		return

	var list_rows := _rows.size()
	var chrome := HEADER_H + DETAIL_H + FOOTER_H + PAD * 2.0
	var available := frame.y * MAX_HEIGHT_FRACTION - chrome
	var shown := clampi(int(available / ROW_H), 1, maxi(list_rows, 1))
	_scroll_to_selection(shown)

	var height := chrome + float(shown) * ROW_H
	# Top right. The developer read-out sits at (14, 12), `driving_hud.gd`'s
	# instrument is bottom-center and its g meter is bottom-right, so this is the
	# one corner of the frame that is free — and it leaves the road ahead visible,
	# which is the condition for tuning while driving at all.
	var panel := Rect2(frame.x - PANEL_W - MARGIN, MARGIN, PANEL_W, height)
	_canvas.draw_rect(panel, COLOR_PANEL)
	_canvas.draw_rect(panel, COLOR_RULE, false, 1.0)

	var x := panel.position.x + PAD
	var width := panel.size.x - PAD * 2.0
	var y := panel.position.y + PAD

	y = _paint_header(x, y, width)

	if _registry == null:
		_text(Vector2(x, y + 20.0),
				"no KartTuning in the %s group" % REGISTRY_GROUP, FONT_SIZE_BODY, COLOR_ALERT)
		_text(Vector2(x, y + 40.0),
				"add one to the scene, or call set_registry()", FONT_SIZE_SMALL, COLOR_DIM)
		return
	if _rows.is_empty():
		_text(Vector2(x, y + 20.0), "the registry served no tunables", FONT_SIZE_BODY, COLOR_ALERT)
		return

	for offset in shown:
		var index := _scroll + offset
		if index >= _rows.size():
			break
		var row: Dictionary = _rows[index]
		_paint_row(row, index, Rect2(x, y + float(offset) * ROW_H, width, ROW_H))

	y += float(shown) * ROW_H + 6.0
	_canvas.draw_line(Vector2(x, y), Vector2(x + width, y), COLOR_RULE, 1.0)

	_paint_detail(x, y + 6.0, width)
	_paint_footer(x, panel.end.y - PAD - 4.0, width, shown)


## Keeps the caret off the edge of the list, so the row being moved onto is already
## visible when the selection gets there.
func _scroll_to_selection(shown: int) -> void:
	if _rows.size() <= shown:
		_scroll = 0
		return
	# Halved because the margin is kept at both ends, and a margin wider than half
	# the window would make the two clamp bounds cross.
	var margin := mini(SCROLL_MARGIN_ROWS, int(float(shown - 1) * 0.5))
	_scroll = clampi(_scroll, _selected - shown + 1 + margin, _selected - margin)
	_scroll = clampi(_scroll, 0, _rows.size() - shown)


func _paint_header(x: float, y: float, width: float) -> float:
	_text(Vector2(x, y + 16.0), "TUNING", FONT_SIZE_TITLE, COLOR_TEXT)

	if _registry == null:
		_text(Vector2(x + 92.0, y + 16.0), "issue #159", FONT_SIZE_SMALL, COLOR_DIM)
		return y + HEADER_H

	# The summary §19 is about. `defended_override_count` is the number that makes a
	# session a different kind of event from one that only turned guesses, so it is
	# on the same line as the total and is colored when it is not zero.
	var changed := int(_registry.changed_count())
	var defended := int(_registry.defended_override_count())
	var hash_hex := String(_registry.tuning_hash_hex())

	if bool(_registry.is_at_defaults()):
		_text(Vector2(x + 92.0, y + 16.0), "everything is at its default",
				FONT_SIZE_BODY, COLOR_GOOD)
	else:
		_text(Vector2(x + 92.0, y + 16.0),
				"%d changed" % changed, FONT_SIZE_BODY, COLOR_CHANGED)
		_text(Vector2(x + 92.0 + 92.0, y + 16.0),
				"%d defended override%s" % [defended, "" if defended == 1 else "s"],
				FONT_SIZE_BODY, COLOR_ALERT if defended > 0 else COLOR_DIM)

	# Printed C++-side. GDScript's `pad_zeros` counts only digit characters and
	# mangles a hex string that starts with a letter — CLAUDE.md records the burn.
	_text(Vector2(x, y + 36.0), "tuning hash  " + hash_hex, FONT_SIZE_SMALL, COLOR_DIM)
	_text(Vector2(x + width - 200.0, y + 36.0),
			"defaults  " + String(_registry.default_hash_hex()), FONT_SIZE_SMALL, COLOR_DIM)
	return y + HEADER_H


## One row: marker, label, value with unit, and the delta from default.
##
## The provenance color is picked off `descriptor()["provenance"]`, the **int**,
## and only the printed word comes from `provenance_name`. `tuning_registry.h` says
## why in its own words: a GDScript `match` on a string is how a fifth provenance
## class would silently become "unknown" instead of failing loudly. The int is the
## enum, and an int this file does not know about falls into `_:` where it is
## visibly wrong rather than quietly grey.
##
## `defended` comes from the descriptor too, never re-derived from the provenance.
## `core/tuning.h::is_defended` owns that rule, and a copy of it here is a copy that
## would still say "Sourced or Measured" the day the rule changes.
func _paint_row(row: Dictionary, index: int, rect: Rect2) -> void:
	var id := int(row["id"])
	var selected := index == _selected
	if selected:
		_canvas.draw_rect(rect, COLOR_SELECTED)
		_canvas.draw_rect(rect, COLOR_TEXT, false, 1.0)
	elif index % 2 == 1:
		_canvas.draw_rect(rect, COLOR_ROW)

	var provenance := int(row["provenance"])
	var color := _provenance_color(provenance)
	var defended := bool(row["defended"])
	var acknowledged := defended and bool(_registry.is_acknowledged(id))
	var at_default := bool(_registry.is_default(id))
	var baseline := rect.position.y + 16.0

	# The caret is a glyph as well as a highlight, so the selection survives a
	# screenshot at any color depth.
	if selected:
		_text(Vector2(rect.position.x + 2.0, baseline), ">", FONT_SIZE_BODY, COLOR_TEXT)

	_text(Vector2(rect.position.x + 16.0, baseline), _provenance_badge(provenance),
			FONT_SIZE_SMALL, color)

	# `LOCK` and `OPEN` in words, not a colored dot: §18, and also because the
	# difference between "you may turn this" and "you already agreed to override
	# this" is exactly the thing a driver must not have to infer from a hue. An
	# acknowledged row keeps `OPEN` for the rest of the session, which is what makes
	# it stay visibly different from an ordinary row long after the gesture.
	if acknowledged:
		_text(Vector2(rect.position.x + 48.0, baseline), "OPEN", FONT_SIZE_SMALL, COLOR_ALERT)
	elif defended:
		_text(Vector2(rect.position.x + 48.0, baseline), "LOCK", FONT_SIZE_SMALL, color)

	_text(Vector2(rect.position.x + 92.0, baseline), String(row["label"]), FONT_SIZE_BODY,
			COLOR_TEXT)

	var decimals := _decimals(float(row["step"]))
	var value: float = _registry.get_value(id)
	var unit := String(row["unit"])
	var value_text := ("%%.%df" % decimals) % value
	if unit != "":
		value_text += " " + unit
	_text(Vector2(rect.position.x + 396.0, baseline), value_text, FONT_SIZE_BODY,
			COLOR_TEXT if at_default else COLOR_CHANGED)

	if at_default:
		return
	# The leading `!` is the file format's own defended-override marker, and it is
	# the same character here on purpose: a row that will be written with a `!` is
	# drawn with one, so the panel and the saved diff say the same thing.
	var delta: float = _registry.delta(id)
	var delta_text := ("%%+.%df" % decimals) % delta
	if defended:
		delta_text = "! " + delta_text
	_text(Vector2(rect.position.x + 596.0, baseline), delta_text, FONT_SIZE_BODY,
			COLOR_ALERT if defended else COLOR_CHANGED)


## Where the constant lives and why it is what it is.
##
## **This block is the point of the whole system**, which is why it gets more
## vertical space than any single row. A person about to turn a knob reads the
## argument for the current number before they turn it; a panel that hid the
## citation behind a keypress would be a slider with a footnote, and `core/tuning.h`
## spends its header explaining why that is the thing not to build.
func _paint_detail(x: float, y: float, width: float) -> void:
	var row := _selected_row()
	if row.is_empty():
		return
	var provenance := int(row["provenance"])
	var color := _provenance_color(provenance)
	var armed := _armed == int(row["id"])

	# Line one is the identity: the key the file will carry, where the number lives,
	# and the class its evidence belongs to, spelled out rather than abbreviated.
	_text(Vector2(x, y + 14.0), String(row["key"]), FONT_SIZE_BODY, COLOR_TEXT)
	_text(Vector2(x + 220.0, y + 14.0), String(row["home"]), FONT_SIZE_SMALL, COLOR_DIM)
	_text(Vector2(x + width - 150.0, y + 14.0),
			"%s  (%s)" % [String(row["provenance_name"]), String(row["owner_name"])],
			FONT_SIZE_SMALL, color)

	# Line two is the room the knob has, so a value pinned at an end of its range is
	# obviously pinned rather than obviously broken.
	var decimals := _decimals(float(row["step"]))
	var format := "%%.%df" % decimals
	_text(Vector2(x, y + 32.0),
			"default %s     range %s to %s     step %s" % [
				format % float(row["default_value"]),
				format % float(row["min_value"]),
				format % float(row["max_value"]),
				format % float(row["step"]),
			], FONT_SIZE_SMALL, COLOR_DIM)

	var citation_y := y + 52.0
	if armed:
		# The armed warning sits above the citation and the citation turns alert —
		# so the second press is confirmed against the argument it is about to
		# override, in the same glance, rather than against a yes/no.
		_canvas.draw_rect(Rect2(x - 4.0, y + 38.0, width + 8.0, DETAIL_H - 44.0),
				Color(COLOR_ALERT.r, COLOR_ALERT.g, COLOR_ALERT.b, 0.16))
		_text(Vector2(x, citation_y),
				"PRESS U AGAIN TO OVERRIDE a %s default. This is what you are overriding:"
						% String(row["provenance_name"]),
				FONT_SIZE_BODY, COLOR_ALERT)
		citation_y += 20.0

	# One line fewer while armed, because the warning above it took that line and the
	# block must not grow into the footer.
	_multiline(Vector2(x, citation_y), String(row["citation"]), width, FONT_SIZE_SMALL,
			COLOR_ALERT if armed else COLOR_TEXT, 4 if armed else 5)


func _paint_footer(x: float, y: float, width: float, shown: int) -> void:
	if _notice != "":
		_text(Vector2(x, y), _notice, FONT_SIZE_BODY, _notice_color)
		return
	# Every key this panel answers to, on the panel. The rule `test_track.gd`'s
	# prototype wrote down: a knob nobody can find is a knob nobody turns, and an
	# undocumented key is a driver concluding the feature is broken.
	var keys := "PgUp/PgDn or d-pad up/down select   [ / ] or d-pad left/right " \
			+ "adjust (hold to repeat)   backspace default   U unlock   " \
			+ "F6 save   F2 close"
	_text(Vector2(x, y), keys, FONT_SIZE_SMALL, COLOR_DIM)
	if _rows.size() > shown:
		_text(Vector2(x + width - 84.0, y),
				"%d-%d of %d" % [_scroll + 1, _scroll + shown, _rows.size()],
				FONT_SIZE_SMALL, COLOR_DIM)


# --- provenance --------------------------------------------------------------


## Color from the **int**, never from `provenance_name`. See `_paint_row`.
func _provenance_color(provenance: int) -> Color:
	match provenance:
		0: return COLOR_SOURCED
		1: return COLOR_MEASURED
		2: return COLOR_DERIVED
		3: return COLOR_UNSOURCED
		_: return COLOR_ALERT # a class this file has not been taught, drawn loudly


## The non-color half of §18. One bracketed letter per class, in the same order the
## enum declares them — most to least defensible.
func _provenance_badge(provenance: int) -> String:
	match provenance:
		0: return "[S]"
		1: return "[M]"
		2: return "[D]"
		3: return "[U]"
		_: return "[?]"


# --- primitives --------------------------------------------------------------


## How many decimals a value is worth showing, from its own step.
##
## Six decimals is what the file format stores and it is unreadable in a row: a
## `steer_gamma` of `3.000000` next to a `frame_torsion` of `193.620000` is two
## columns of noise. One digit finer than the step is the most a press can ever
## reveal, so that is what is drawn — `max_lock`'s 0.0087 rad step gets four
## decimals, `frame_torsion`'s 10 N m/deg gets one.
func _decimals(step: float) -> int:
	if step <= 0.0:
		return 3
	return clampi(int(ceil(-log(step) / log(10.0))) + 1, 1, 6)


func _text(at: Vector2, value: String, size: int, color: Color) -> void:
	if _font == null:
		return
	_canvas.draw_string(_font, at, value, HORIZONTAL_ALIGNMENT_LEFT, -1, size, color)


func _multiline(at: Vector2, value: String, width: float, size: int, color: Color,
		lines: int) -> void:
	if _font == null:
		return
	_canvas.draw_multiline_string(_font, at, value, HORIZONTAL_ALIGNMENT_LEFT, width, size,
			lines, color)
