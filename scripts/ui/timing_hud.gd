class_name TimingHud
extends Control

## The timing screen: lap, sectors, deltas, and whether the lap still counts.
##
##     var hud := TimingHud.new()
##     canvas_layer.add_child(hud)
##     hud.bind(runner)
##
## Everything on it is served by `KartLapTimer` — `src/session/kart_session.h` — and
## by `SessionRunner`. **Nothing here computes a time.** `lap_timing.h` counts in
## ticks for reasons this file must not undermine: a lap time built from
## `_process`'s delta would get faster on a slow machine, and this overlay redraws
## at the frame rate.
##
## ## What is on it, and the one thing that decides that list
##
## `driving_hud.gd` states the test and it applies here: *"would a driver act on it
## inside one corner"*. For a timing screen that becomes a sharper question, because
## a lap time is not actionable inside a corner and a **sector delta** is. So the
## sector delta is the largest figure on the panel and the running lap time is not.
##
##   * the running lap, and the sector it is in
##   * the delta to the best lap's same sector, negative being ahead
##   * last lap, with the reason it did not count when it did not
##   * best lap, its three sectors, and the theoretical best those add up to
##   * how many laps counted and how many were struck out
##   * whether the lap in progress is already spoiled, and by what
##
## ## Why the lap in progress gets a banner rather than a color
##
## A struck-out lap is the one piece of information here a driver has to act on
## immediately — there is no point finishing a lap that has already been thrown away
## — and `#73` requires that nothing be carried by hue alone. So an invalid lap in
## progress puts a **word** on the panel saying what happened, in the same place
## every time, and the time it is next to is drawn dimmed. Run it through a
## monochrome filter and the word is still there.
##
## ## The look is not designed here
##
## `GAMEDESIGN.md` §9 and [#171](https://github.com/skiretic/kartgame/issues/171):
## the front end's visual pass is blocked on references — *"live timing screens,
## classification sheets"* — and it is not this file's. What this is instead is the
## house style the project already has: `telemetry_panel.gd`'s and
## `tuning_panel.gd`'s Okabe-Ito palette and dark panel, so that a driver looking at
## three overlays is looking at one project. When #171 lands with photographs of a
## real timing tower, the layout below is what changes and nothing above the
## `_draw()` line has to.
##
## **There is no keybinding and nothing on this panel names one.** This project's
## repeated failure is a control printed on screen that nothing reads —
## [#169](https://github.com/skiretic/kartgame/issues/169), four cases in one day —
## and a timing screen with an invented toggle key would be the fifth. It is visible
## whenever the scene's HUD is, and that is the whole of its interface.

## The runner. Assigned through `bind()`, because the panel and the session are built
## in the same function and reading a null runner would draw an empty table forever.
var runner: SessionRunner = null

## Machine reason name to what a driver reads.
##
## `lap_timing.h`'s `lap_invalid_reason_name` is the other end of this and it is
## deliberately a second table rather than the same one: ADR-0044 rule 2 keeps UI
## text out of `src/` — partly because `godot::String` decodes a bare `const char *`
## as Latin-1 so a non-ASCII character in a C++ literal arrives as mojibake, and any
## language this would be translated into breaks that immediately. So the C++ side
## owns a stable machine name and this side owns the sentence.
##
## A reason absent from this table falls back to the machine name, which is ugly and
## readable. Silently drawing nothing would hide the one thing the row exists for.
const REASONS: Dictionary = {
	"off_track": "ALL FOUR WHEELS OFF",
	"missed_mark": "CHECKPOINT MISSED",
	"respawned": "RESTARTED WITH HELP",
	"deleted": "DELETED BY PENALTY",
	"out_lap": "OUT LAP",
}

## Okabe-Ito, the same eight `telemetry_panel.gd` and `tuning_panel.gd` use, in the
## same hex, so the three overlays read as one instrument.
const COLOR_AHEAD := Color(0.000, 0.620, 0.451) # 009E73 bluish green
const COLOR_BEHIND := Color(0.835, 0.369, 0.000) # D55E00 vermillion
const COLOR_BEST := Color(0.941, 0.894, 0.259) # F0E442 yellow

const COLOR_PANEL := Color(0.04, 0.05, 0.07, 0.88)
const COLOR_RULE := Color(1.0, 1.0, 1.0, 0.12)
const COLOR_TEXT := Color(0.88, 0.90, 0.94)
const COLOR_DIM := Color(0.60, 0.63, 0.70)
const COLOR_FAINT := Color(0.60, 0.63, 0.70, 0.45)

## Layout in pixels at 1080p, scaled by viewport height.
##
## By height and not by width, which is `driving_hud.gd`'s reasoning and its
## `frame.y / 1080` reference is reused verbatim so the two overlays grow together.
## On an ultrawide, scaling by width would put a timing tower a third of the way
## across the screen.
const PANEL_W := 320.0
const MARGIN := 16.0
const PAD := 12.0
const ROW_H := 22.0
const RULE_GAP := 8.0

const FONT_LABEL := 12
const FONT_BODY := 16
const FONT_HERO := 34

var _font: Font
var _s := 1.0
var _sb_panel := StyleBoxFlat.new()

## The pen, in panel-local pixels. Carried as a member rather than threaded through
## every draw call because the rows are written top to bottom and a row that has to
## be told where it is is a row that can be told the wrong place.
var _y := 0.0
var _panel := Rect2()

## True during the sizing pass. Every primitive still advances the pen and skips its
## draw call, which is what lets `_lay_out` be the single owner of the row list.
var _measuring := false


func _ready() -> void:
	# Nothing to draw on, and every primitive below would fail individually — about
	# forty errors with a stack trace each, per tick, which is `driving_hud.gd`'s
	# recorded experience of burying a scenario report under something that looked
	# like a solver fault. `tools/verify/session_probe.gd` runs headless and reads the
	# runner directly, so this overlay has nothing to contribute there.
	if DisplayServer.get_name() == "headless":
		hide()
		set_process(false)
		return
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_preset(Control.PRESET_FULL_RECT)
	_font = ThemeDB.fallback_font
	_sb_panel.bg_color = COLOR_PANEL
	_sb_panel.set_corner_radius_all(8)
	_sb_panel.border_color = Color(1.0, 1.0, 1.0, 0.10)
	_sb_panel.set_border_width_all(1)
	get_viewport().size_changed.connect(queue_redraw)


## Called once by whoever owns the session. `test_track.gd` does.
func bind(session_runner: SessionRunner) -> void:
	runner = session_runner
	queue_redraw()


## Redrawn at the frame rate, and it reads only.
##
## The panel holds no sampled history of its own, unlike `driving_hud.gd`'s g trail —
## everything on it is a number `KartLapTimer` already keeps, so there is nothing to
## accumulate at the physics rate and no reason to run `_physics_process` at all.
func _process(_delta: float) -> void:
	queue_redraw()


## Laid out twice: once to find out how tall it is, once to draw it.
##
## **The background box has to be drawn before the rows and its height is not known
## until after them**, which is the standard bind in an immediate-mode `_draw` and has
## exactly two solutions: count the rows in a second place, or run the layout twice.
##
## Counting was tried and was wrong on the first render — the box came out 46 px short
## and the BEST and LAPS rows were drawn on the sky below it. It is wrong in the way
## that matters too, which is that it is a **second owner of the row list**: the count
## was `ROW_H * 8.0` against a layout that emits 10.44 rows once the two 0.72-height
## labels and the four history rows are counted, and it would have gone wrong again
## the first time somebody added a row. The sector strip is `best_sectors().size()`
## wide and `SessionRunner.SECTOR_COUNT` is a placeholder until ADR-0046's
## `track.json` authors real splits, so that day was coming.
##
## Running the layout twice costs one extra pass over a dozen rows and cannot drift.
## `_measuring` suppresses every draw call while still advancing the pen.
func _draw() -> void:
	if runner == null:
		return
	var frame := get_viewport_rect().size
	_s = frame.y / 1080.0
	var origin := Vector2(frame.x - (PANEL_W + MARGIN) * _s, MARGIN * _s)
	var timer := runner.timer()

	_measuring = true
	_panel = Rect2(origin, Vector2(PANEL_W * _s, 0.0))
	_y = origin.y + PAD * _s
	_lay_out(timer)
	var height := _y - origin.y + PAD * _s

	_measuring = false
	_panel = Rect2(origin, Vector2(PANEL_W * _s, height))
	draw_style_box(_sb_panel, _panel)
	_y = origin.y + PAD * _s
	_lay_out(timer)


## The rows, in order. Called once measuring and once drawing, so it must be pure
## apart from the pen — anything conditional in here has to depend on the timer and
## not on `_measuring`, or the two passes disagree and the box is the wrong size again.
func _lay_out(timer: KartLapTimer) -> void:
	_draw_header()
	if timer == null:
		# Refused, or configured and refused. The panel says so rather than showing an
		# empty table, which would read as a session that is running and has produced
		# nothing.
		_label(runner.refusal() if runner.refusal() != "" else "no session")
		return

	_draw_delta(timer)
	_draw_lap(timer)
	_rule()
	_draw_sectors(timer)
	_rule()
	_draw_history(timer)


# --- the rows -------------------------------------------------------------------


## Session type and state, on one line.
##
## The state is on the panel because `SessionRunner`'s states are the difference
## between a kart that is not being timed and a kart that is — a driver holding the
## throttle during `held` needs to know the reason nothing is happening is the hold,
## not the throttle.
func _draw_header() -> void:
	var result := runner.result()
	var type_name := "session"
	if not result.is_empty():
		type_name = String(result["type_name"])
	_row(type_name.to_upper(), runner.state_name().to_upper(), COLOR_TEXT, COLOR_DIM)


## The sector delta, as the largest figure on the panel. See the header for why.
##
## `sector_delta_to_best()` returns a negative number both for "ahead" and for "no
## comparison", which is why `has_best()` is asked first — `kart_session.h` calls
## that out as the reason the two are a pair. Without the guard, the first lap of a
## session would show a large negative delta and read as the fastest sector ever
## driven.
func _draw_delta(timer: KartLapTimer) -> void:
	var text := "--.---"
	var color := COLOR_FAINT
	if timer.has_best() and runner.is_running():
		var delta := timer.sector_delta_to_best()
		text = SessionRunner.format_delta(delta)
		color = COLOR_AHEAD if delta < 0.0 else COLOR_BEHIND
	_label("SECTOR %d vs BEST" % (timer.sector() + 1))
	var at := Vector2(_panel.end.x - PAD * _s, _y + FONT_HERO * _s * 0.82)
	_digits(at, text, FONT_HERO, color, true)
	_y += FONT_HERO * _s + RULE_GAP * _s


## The lap in progress, and the word that says it is already gone.
func _draw_lap(timer: KartLapTimer) -> void:
	var reason := timer.current_reason()
	var spoiled := reason != "valid"
	_row("LAP", SessionRunner.format_time(timer.lap_time()),
		COLOR_TEXT, COLOR_DIM if spoiled else COLOR_TEXT)
	if spoiled:
		# The banner. A word, in the same place every time, because this is the one
		# figure a driver acts on immediately and #73 forbids carrying it by hue.
		_row("", String(REASONS.get(reason, reason.to_upper())), COLOR_TEXT, COLOR_BEHIND)
	else:
		_row("", "COUNTING", COLOR_TEXT, COLOR_FAINT)


## The best lap's sectors, and the theoretical best they add up to.
##
## `optimal_lap()` is drawn beside them rather than mixed in with the lap times, which
## is `lap_timing.h`'s own distinction: *"a number a driver chases and a number that
## is never a lap time"*. A driver who read it as a lap they had done would be reading
## a lap nobody drove.
func _draw_sectors(timer: KartLapTimer) -> void:
	var sectors := timer.best_sectors()
	_label("BEST SECTORS")
	var columns := maxi(sectors.size(), 1)
	var width := (_panel.size.x - PAD * 2.0 * _s) / float(columns)
	for index in columns:
		var at := Vector2(_panel.position.x + PAD * _s + width * float(index + 1) - 4.0 * _s,
			_y + FONT_BODY * _s)
		var time := sectors[index] if index < sectors.size() else -1.0
		# The sector the kart is in is marked by the *label* under it, not by a color
		# on the figure, for the same monochrome reason as the banner.
		var color := COLOR_BEST if index == timer.sector() else COLOR_TEXT
		_digits(at, SessionRunner.format_time(time), FONT_BODY, color, true)
	_y += ROW_H * _s
	_row("OPTIMAL", SessionRunner.format_time(timer.optimal_lap()), COLOR_DIM, COLOR_DIM)


func _draw_history(timer: KartLapTimer) -> void:
	var last_reason := timer.last_reason()
	_row("LAST", SessionRunner.format_time(timer.last_time()), COLOR_TEXT,
		COLOR_TEXT if timer.last_was_valid() else COLOR_DIM)
	if timer.has_last() and not timer.last_was_valid():
		_row("", String(REASONS.get(last_reason, last_reason.to_upper())),
			COLOR_TEXT, COLOR_FAINT)
	else:
		_y += ROW_H * _s
	_row("BEST", SessionRunner.format_time(timer.best_time()), COLOR_TEXT, COLOR_BEST)
	# One sentence with placeholders, not three fragments. ADR-0044 rule 1.
	_row("LAPS", "%d counted, %d struck out" % [
		timer.valid_lap_count(), timer.invalid_lap_count(),
	], COLOR_TEXT, COLOR_DIM)


# --- primitives -----------------------------------------------------------------


## A label on the left and a value on the right, advancing the pen one row.
func _row(label: String, value: String, label_color: Color, value_color: Color) -> void:
	var baseline := _y + FONT_BODY * _s
	if label != "":
		_text(Vector2(_panel.position.x + PAD * _s, baseline), label, FONT_LABEL,
			label_color, false)
	_digits(Vector2(_panel.end.x - PAD * _s, baseline), value, FONT_BODY, value_color, true)
	_y += ROW_H * _s


## A small caps label on its own line.
func _label(text: String) -> void:
	_text(Vector2(_panel.position.x + PAD * _s, _y + FONT_LABEL * _s), text, FONT_LABEL,
		COLOR_FAINT, false)
	_y += ROW_H * _s * 0.72


func _rule() -> void:
	_y += RULE_GAP * _s * 0.5
	if not _measuring:
		draw_line(Vector2(_panel.position.x + PAD * _s, _y),
			Vector2(_panel.end.x - PAD * _s, _y), COLOR_RULE, 1.0 * _s)
	_y += RULE_GAP * _s * 0.5


func _text(at: Vector2, value: String, size: int, color: Color, right: bool) -> void:
	if _measuring:
		return
	var px := int(float(size) * _s)
	var origin := at
	if right:
		origin.x -= _font.get_string_size(value, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px).x
	draw_string(_font, origin, value, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px, color)


## Text with **digits on a fixed pitch**, which is the whole reason this exists.
##
## `ThemeDB.fallback_font` is whatever Godot ships and it is a proportional sans, so
## a `1` is narrower than a `0`. On a lap time counting up at 120 Hz that makes every
## figure on the panel shuffle sideways several times a second, and on a delta it
## makes the sign move — which is the one character a driver is reading. Shipping a
## tabular typeface would be an asset for eleven glyphs, and `driving_hud.gd` solved
## the same problem by drawing seven-segment digits itself; this is the cheap version
## of the same decision, and it stays font-independent.
##
## The pitch is the widest digit's own width, measured from the font rather than
## assumed, so it is correct for whichever font the project ends up with.
func _digits(at: Vector2, value: String, size: int, color: Color, right: bool) -> void:
	if _measuring:
		return
	var px := int(float(size) * _s)
	var pitch := 0.0
	for glyph in "0123456789":
		pitch = maxf(pitch, _font.get_string_size(glyph, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px).x)

	var total := 0.0
	for glyph in value:
		total += pitch if glyph.is_valid_int() else \
			_font.get_string_size(glyph, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px).x

	var x := at.x - total if right else at.x
	for glyph in value:
		if glyph.is_valid_int():
			# Centered in its cell, so a `1` sits where a `0` would rather than against
			# the left edge of the space a `0` needs.
			var width := _font.get_string_size(glyph, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px).x
			draw_string(_font, Vector2(x + (pitch - width) * 0.5, at.y), glyph,
				HORIZONTAL_ALIGNMENT_LEFT, -1.0, px, color)
			x += pitch
		else:
			draw_string(_font, Vector2(x, at.y), glyph, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px,
				color)
			x += _font.get_string_size(glyph, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px).x
