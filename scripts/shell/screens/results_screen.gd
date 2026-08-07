extends ShellScreen

## Plate 2. The session classification — a federation document, not a menu.
##
## The paper family, and everything about the page furniture is arguing for that
## reading: a 2 px rule under the titleblock and above the footer against 1 px
## rules inside the body, a session tree down the left, the best and the optimal
## pinned above the table rather than buried in it. Drop those and the same data
## is a spreadsheet.
##
## ## What the emphasis means, because on a classification it is never decoration
##
## - **The session best is inverted** — ink ground, paper text — and not tinted.
##   Real sheets invert; a green cell on a classification reads as a status light,
##   and the one thing a classification is not is live.
## - **A personal best is bold.** A lap that beat what the profile had stored for
##   this circuit coming in, and a sector that is the best anybody drove in this
##   session. Weight, not hue.
## - **A struck lap strikes the time cell only.** The sectors stay upright and
##   readable, because a cut lap very often holds your best S1 and deleting it
##   from the page would delete that. The reason goes in the Note column in
##   `ST_SLOWER`, which is the one place on this sheet a state color appears.
## - **The current round in the tree carries a 3 px accent left border**, never a
##   filled bar. `ShellTheme.SELECT_BORDER_PX`.
##
## ## The pinned best is the minimum over the *valid* laps
##
## Not over every lap, and this is the sheet's single most important line of
## arithmetic. `shell_probe.gd::_synthetic_ledger()` plants a struck lap that is
## faster than every valid one precisely to catch a sheet that sorts on time: a
## classification that pinned an off-track lap as the session best would be
## publishing a time the driver did not legally set.
##
## The figure is computed from the rows rather than read from
## `result["best_lap_s"]`, even though the runner's timer already agrees. A sheet
## must be able to point at the row its headline number came from — a hero figure
## with no line in the table under it is unauditable, which is the opposite of what
## a classification is for.
##
## ## Deliberately not built
##
## The eight-row live timing grid on plate 1 is the *race* version of this screen
## and it needs a field. `SessionRunner.configure()` refuses `entry_count > 1` by
## name, so a standings grid today would be one row of the player against nobody —
## exactly the stubbed mode `GAMEDESIGN.md` §13 forbids. Plate 3, the championship
## classification, is Phase C for the same reason.

## `scripts/ui/timing_hud.gd`'s reason table, reused rather than duplicated.
##
## Loaded by path and not as `TimingHud`, and the same for the store below: a
## GDScript `class_name` is invisible until an editor import has populated
## `.godot/global_script_class_cache.cfg`, and this screen is loaded by a
## `--script` gate that runs no such import. A stale cache would turn "the
## identifier is not declared" into "results_screen.gd is not built", which is a
## diagnosis nobody would arrive at quickly.
const TimingHudScript := preload("res://scripts/ui/timing_hud.gd")
const BestLapStoreScript := preload("res://scripts/shell/best_lap_store.gd")

## The rounds of a race weekend, in order, from the plate. **Practice is the only
## one that exists**, and the rest are drawn dim rather than omitted for ADR-0052's
## reason: a tree that quietly listed one round would read as a game whose weekend
## has one session, and a tree that lists five with four dim reads as a game being
## built. Nothing here is clickable, because there is nothing to navigate to — a
## focusable row that does nothing is worse than a label.
const ROUNDS: PackedStringArray = [
	"Practice", "Qualifying Practice", "Qualifying Heat", "Super Heat", "Final",
]

## The plate's page margins at the 900 px design height.
const MARGIN_X := 56
const MARGIN_Y := 44

## Column gutter and row pitch in the lap table. 18 px is wide enough that a
## right-aligned number column does not run into the one before it at `T_BODY`,
## which is the only thing a table's horizontal separation has to buy.
const TABLE_H_SEP := 18
const TABLE_V_SEP := 5

## The inverted session-best cell's padding, in pixels. Small on purpose: the ink
## block should hug the digits, because one that filled the column would read as a
## rule across the sheet rather than as an emphasized number.
const INVERT_PAD_X := 5.0
const INVERT_PAD_Y := 1.0

## The left column's width. Fixed rather than shrink-to-fit so the sheet body does
## not move sideways between a session tree of five rounds and one of three.
const TREE_WIDTH := 172.0

var _result: Dictionary = {}
var _rows: Array = []
var _sector_count := 0

## The classification's headline: the fastest **valid** lap, or negative when the
## session set none. `shell_probe.gd` reads this by name.
var _pinned_best_s := -1.0
var _pinned_best_lap := 0
var _pinned_sectors := PackedFloat64Array()

## The best time driven in each sector by any valid lap, for the bold marks and for
## the optimal. Parallel to the sector columns; an entry stays negative when no
## valid lap posted that sector.
var _sector_bests := PackedFloat64Array()

## What the profile had stored for this circuit *before* this session, from the
## store's own reading. A lap under it is a personal best and is set in bold.
var _previous_best_s := -1.0

var _rows_drawn := 0
var _footer_line := ""
var _back: Button


func title() -> String:
	return "results"


# --- build ---------------------------------------------------------------------


func build() -> void:
	# **The screen drains the result, the shell does not.** `_open_first_screen()`
	# only asks `SessionRequest.has_result()` before choosing which screen to open;
	# nothing in `shell_root.gd` calls `take_result()`. If this line moves, the sheet
	# is blank and the next visit to the paddock re-opens a stale one.
	_result = SessionRequest.take_result()
	_read_rows()
	_measure()
	_file_best_lap()

	add_child(ShellTheme.ground(true))

	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	for side: String in ["left", "right"]:
		margin.add_theme_constant_override("margin_" + side, MARGIN_X)
	for side: String in ["top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, MARGIN_Y)
	add_child(margin)

	var page := VBoxContainer.new()
	page.add_theme_constant_override("separation", 10)
	margin.add_child(page)

	page.add_child(_titleblock())
	page.add_child(ShellTheme.rule(true, true))

	var body := HBoxContainer.new()
	body.add_theme_constant_override("separation", 28)
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	page.add_child(body)
	body.add_child(_session_tree())
	body.add_child(_sheet())

	page.add_child(ShellTheme.rule(true, true))
	page.add_child(_footer())


# --- reading what came back ------------------------------------------------------


func _read_rows() -> void:
	var carried: Variant = _result.get("laps", [])
	if carried is Array:
		for entry: Variant in carried:
			if entry is Dictionary:
				_rows.append(entry)

	for row: Dictionary in _rows:
		_sector_count = maxi(_sector_count, _sectors_of(row).size())
	if _sector_count == 0:
		# No lap posted a split — a session that ended on the out lap. The header row
		# still needs a column count, and the *track's* is the honest one:
		# `lap_timing.h` makes the partition data, and a sheet that drew three columns
		# on a two-sector circuit would be inventing a sector.
		_sector_count = int(_result.get("sector_count", 0))


## The classification's arithmetic, all of it, in one place.
##
## Every figure here is over the **valid** rows. A struck lap contributes its
## sectors to the table and to nothing else: it cannot be the session best, and it
## cannot donate a sector to the optimal, because the optimal is a lap that could
## have been driven legally.
func _measure() -> void:
	_sector_bests.resize(_sector_count)
	_sector_bests.fill(-1.0)

	for row: Dictionary in _rows:
		if not bool(row.get("valid", false)):
			continue
		var time_s := float(row.get("time_s", -1.0))
		if time_s > 0.0 and (_pinned_best_s < 0.0 or time_s < _pinned_best_s):
			_pinned_best_s = time_s
			_pinned_best_lap = int(row.get("lap", 0))
			_pinned_sectors = _sectors_of(row)

		var sectors := _sectors_of(row)
		for index: int in mini(sectors.size(), _sector_count):
			var split := sectors[index]
			if split > 0.0 and (_sector_bests[index] < 0.0 or split < _sector_bests[index]):
				_sector_bests[index] = split

	if _rows.is_empty() and bool(_result.get("has_best", false)):
		# No ledger — an older scene, or a session whose ledger was never attached.
		# The runner's own figure is the fallback, and the table below will be empty,
		# which is the visible symptom of exactly that.
		_pinned_best_s = float(_result.get("best_lap_s", -1.0))
		var carried: Variant = _result.get("best_sectors", PackedFloat64Array())
		if carried is PackedFloat64Array:
			_pinned_sectors = carried


## Write the lap to the profile, and keep what the store said for the footer.
##
## The store refuses on its own for every case that matters — no timed lap, no
## track id, an empty ghost id — so there is no condition tested twice here. The
## one thing this does decide is that a sheet opened with **no session at all**
## does not call it: an empty dictionary is a screen shot for a still, not a lap
## somebody drove.
func _file_best_lap() -> void:
	if _result.is_empty():
		_footer_line = "Opened without a session — there is nothing to classify."
		return
	if shell == null:
		return

	var profile: KartProfile = shell.profile()
	var outcome: Dictionary = BestLapStoreScript.record(profile, _result)
	_previous_best_s = float(outcome.get("previous_s", -1.0))

	_footer_line = BestLapStoreScript.summary(outcome)
	if not _footer_line.is_empty():
		return
	# Nothing was filed. Say which of the reasons it was rather than leaving the
	# footer blank — a lap time that silently does not save is the exact failure
	# this whole push exists to close, and a blank footer is how it stays silent.
	var warnings: PackedStringArray = outcome.get("warnings", PackedStringArray())
	if not warnings.is_empty():
		_footer_line = "Not filed: %s" % warnings[0]


# --- the titleblock ---------------------------------------------------------------


func _titleblock() -> Control:
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 24)

	var left := VBoxContainer.new()
	left.add_theme_constant_override("separation", 2)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_child(ShellTheme.kicker(
			"Session classification · %s" % _session_kind(), ShellTheme.PAPER_SOFT))
	left.add_child(ShellTheme.label(_masthead(), ShellTheme.T_TITLE,
			ShellTheme.PAPER_INK, ShellTheme.Weight.BOLD))
	bar.add_child(left)

	var right := VBoxContainer.new()
	right.add_theme_constant_override("separation", 1)
	right.alignment = BoxContainer.ALIGNMENT_END
	# The disclaimer is on the real sheets and it is on this one for the same
	# reason: a document that looks official has to say what it is not.
	for line: String in _meta_lines():
		var note := ShellTheme.label(line, ShellTheme.T_FOOT, ShellTheme.PAPER_SOFT)
		note.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		note.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		right.add_child(note)
	bar.add_child(right)
	return bar


## `Circuit · layout · class`, from what the track scene carried. The runner deals
## in a slug; a masthead does not.
func _masthead() -> String:
	var track := String(_result.get("track_name", ""))
	if track.is_empty():
		track = String(_result.get("track", "No circuit"))
	var layout := String(_result.get("layout_name", ""))
	if layout.is_empty():
		return track
	return "%s · %s" % [track, layout]


func _session_kind() -> String:
	var kind := String(_result.get("type_name", ""))
	return kind if not kind.is_empty() else "Practice"


func _meta_lines() -> PackedStringArray:
	var lines := PackedStringArray(["For information purposes",
			"No official / regulatory value"])
	if _result.is_empty():
		return lines
	var laps := int(_result.get("laps_completed", 0))
	var valid := int(_result.get("valid_laps", 0))
	lines.append("%d lap%s · %d valid" % [laps, "" if laps == 1 else "s", valid])
	return lines


# --- the session tree ---------------------------------------------------------------


## The rounds down the left. Structure without invention: Practice is the round
## that ran and it is marked, the other four are the weekend that M7 will build and
## they are dim. None of them is a control.
func _session_tree() -> Control:
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 3)
	column.custom_minimum_size = Vector2(TREE_WIDTH, 0.0)
	column.add_child(ShellTheme.kicker("this round", ShellTheme.PAPER_SOFT))

	var current := _session_kind()
	for round_name: String in ROUNDS:
		column.add_child(_tree_entry(round_name, round_name == current))
	return column


## One round. The current one carries the accent left border — `SELECT_BORDER_PX`,
## the shell's one marker for "this is the selected thing" — and never a fill.
func _tree_entry(text: String, current: bool) -> Control:
	var holder := MarginContainer.new()
	holder.add_theme_constant_override("margin_left", 10)
	holder.add_theme_constant_override("margin_top", 2)
	holder.add_theme_constant_override("margin_bottom", 2)

	if current:
		var box := StyleBoxFlat.new()
		box.bg_color = _clear()
		box.border_color = ShellTheme.ACCENT
		box.border_width_left = int(ShellTheme.SELECT_BORDER_PX)
		var frame := PanelContainer.new()
		frame.add_theme_stylebox_override("panel", box)
		frame.add_child(holder)
		holder.add_child(ShellTheme.label(text, ShellTheme.T_BODY, ShellTheme.PAPER_INK,
				ShellTheme.Weight.SEMIBOLD))
		return frame

	holder.add_child(ShellTheme.label(text, ShellTheme.T_BODY, ShellTheme.PAPER_SOFT))
	return holder


# --- the sheet ----------------------------------------------------------------------


func _sheet() -> Control:
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 8)
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL

	column.add_child(_pinned())
	column.add_child(ShellTheme.rule(false, true))

	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	# A classification never scrolls sideways. A 30-lap Final does scroll down, which
	# is why the container is here at all rather than a bare table.
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.add_child(_table())
	column.add_child(scroll)
	return column


## The four pinned facts above the table.
func _pinned() -> Control:
	var block := VBoxContainer.new()
	block.add_theme_constant_override("separation", 2)

	var headline := HBoxContainer.new()
	headline.add_theme_constant_override("separation", 14)
	headline.add_child(ShellTheme.label(SessionRunner.format_time(_pinned_best_s),
			ShellTheme.T_HERO, ShellTheme.PAPER_INK, ShellTheme.Weight.BOLD))
	var caption := VBoxContainer.new()
	caption.alignment = BoxContainer.ALIGNMENT_CENTER
	caption.add_child(ShellTheme.kicker(_best_caption(), ShellTheme.PAPER_SOFT))
	caption.add_child(ShellTheme.label(_splits_line(), ShellTheme.T_BODY,
			ShellTheme.PAPER_SOFT))
	headline.add_child(caption)
	block.add_child(headline)

	# **The optimal is never presented as a lap time.** `lap_timing.h` draws the
	# distinction in its own words — *"a number a driver chases and a number that is
	# never a lap time"* — and the label carries it, because a driver who read this
	# as a lap they had done would be reading a lap nobody drove.
	block.add_child(_pinned_line("Optimal — sum of sector bests, nobody drove it",
			_optimal_line()))
	block.add_child(_pinned_line("Emphasis, per the real sheets",
			"inverted = session best · bold = personal best · struck = lap deleted"))
	return block


func _pinned_line(label_text: String, value_text: String) -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	var head := ShellTheme.kicker(label_text, ShellTheme.PAPER_SOFT)
	head.custom_minimum_size = Vector2(300.0, 0.0)
	row.add_child(head)
	row.add_child(ShellTheme.label(value_text, ShellTheme.T_BODY, ShellTheme.PAPER_INK))
	return row


func _best_caption() -> String:
	if _pinned_best_s <= 0.0:
		return "Best lap — none set"
	if _pinned_best_lap <= 0:
		return "Best lap"
	if _previous_best_s > 0.0 and _pinned_best_s < _previous_best_s:
		return "Best lap — lap %d · new personal best" % _pinned_best_lap
	return "Best lap — lap %d" % _pinned_best_lap


func _splits_line() -> String:
	if _pinned_sectors.is_empty():
		return "No splits recorded"
	var parts := PackedStringArray()
	for split: float in _pinned_sectors:
		parts.append("%.3f" % split)
	return " · ".join(parts)


## The theoretical lap: every sector's best, added up.
##
## Only when **every** sector has a valid sample. Summing the three that exist and
## calling the total an optimal lap would publish a lap that is short by a sector,
## and it would be short in a way nothing on the page shows.
func _optimal_line() -> String:
	var total := 0.0
	for index: int in _sector_bests.size():
		if _sector_bests[index] <= 0.0:
			return "—"
		total += _sector_bests[index]
	if _sector_bests.is_empty() or _pinned_best_s <= 0.0:
		return "—"
	return "%s (%s)" % [SessionRunner.format_time(total),
			SessionRunner.format_delta(total - _pinned_best_s)]


# --- the lap table ---------------------------------------------------------------------


func _table() -> Control:
	var grid := GridContainer.new()
	grid.columns = _sector_count + 4
	grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grid.add_theme_constant_override("h_separation", TABLE_H_SEP)
	grid.add_theme_constant_override("v_separation", TABLE_V_SEP)

	_add_head(grid, "Lap")
	_add_head(grid, "Time")
	for index: int in _sector_count:
		_add_head(grid, "S%d" % (index + 1))
	_add_head(grid, "Spd")
	var note_head := ShellTheme.kicker("Note", ShellTheme.PAPER_SOFT)
	note_head.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	note_head.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grid.add_child(note_head)

	for row: Dictionary in _rows:
		_add_row(grid, row)
		_rows_drawn += 1
	return grid


func _add_head(grid: GridContainer, text: String) -> void:
	var head := ShellTheme.kicker(text, ShellTheme.PAPER_SOFT)
	head.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	head.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grid.add_child(head)


func _add_row(grid: GridContainer, row: Dictionary) -> void:
	var valid := bool(row.get("valid", false))
	var time_s := float(row.get("time_s", -1.0))
	var sectors := _sectors_of(row)

	grid.add_child(_number("%d" % int(row.get("lap", 0)), ShellTheme.PAPER_INK))

	if not valid:
		# The strike is on this cell and this cell only. Everything to the right of it
		# is drawn exactly as a valid lap's would be.
		grid.add_child(_struck(SessionRunner.format_time(time_s)))
	elif time_s > 0.0 and absf(time_s - _pinned_best_s) < 1e-9:
		grid.add_child(_inverted(SessionRunner.format_time(time_s)))
	elif _previous_best_s > 0.0 and time_s > 0.0 and time_s < _previous_best_s:
		grid.add_child(_number(SessionRunner.format_time(time_s), ShellTheme.PAPER_INK,
				ShellTheme.Weight.BOLD))
	else:
		grid.add_child(_number(SessionRunner.format_time(time_s), ShellTheme.PAPER_INK))

	for index: int in _sector_count:
		if index >= sectors.size() or sectors[index] <= 0.0:
			grid.add_child(_number("—", ShellTheme.PAPER_SOFT))
			continue
		# Bold marks the best anybody drove in this sector this session — which is
		# what the plate's own numbers do, lap 9 taking S1 and S2 and lap 11 taking
		# S3. A struck lap's sector is never one of them; `_measure()` skips it.
		var best := valid and absf(sectors[index] - _sector_bests[index]) < 1e-9
		grid.add_child(_number("%.3f" % sectors[index], ShellTheme.PAPER_INK,
				ShellTheme.Weight.BOLD if best else ShellTheme.Weight.REGULAR))

	var speed := float(row.get("speed_kmh", -1.0))
	grid.add_child(_number("%.0f" % speed if speed >= 0.0 else "—",
			ShellTheme.PAPER_INK if speed >= 0.0 else ShellTheme.PAPER_SOFT))

	var reason := String(row.get("reason", ""))
	var note: Label
	if not valid:
		note = ShellTheme.kicker(_reason_text(reason), ShellTheme.ST_SLOWER)
	elif time_s > 0.0 and absf(time_s - _pinned_best_s) < 1e-9:
		note = ShellTheme.kicker("best", ShellTheme.PAPER_SOFT)
	else:
		note = ShellTheme.label("", ShellTheme.T_COLHEAD, ShellTheme.PAPER_SOFT)
	note.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	note.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grid.add_child(note)


## `timing_hud.gd:261`'s own expression, reused rather than re-tabulated. A reason
## the HUD does not know about falls through as itself, which is how a new
## `LapInvalidReason` shows up as a word rather than as a blank cell.
func _reason_text(reason: String) -> String:
	if reason.is_empty():
		return ""
	return String(TimingHudScript.REASONS.get(reason, reason.to_upper()))


# --- cells ------------------------------------------------------------------------


func _number(text: String, color: Color, weight := ShellTheme.Weight.REGULAR) -> Label:
	var cell := ShellTheme.label(text, ShellTheme.T_BODY, color, weight)
	cell.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	cell.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return cell


## The session best: ink ground, paper text.
##
## `SIZE_SHRINK_END` so the block hugs the digits at the right edge of its column.
## Filling the column would draw a bar across the sheet, which is the fill the
## whole theme is written to avoid.
func _inverted(text: String) -> Control:
	var box := StyleBoxFlat.new()
	box.bg_color = ShellTheme.PAPER_INK
	box.content_margin_left = INVERT_PAD_X
	box.content_margin_right = INVERT_PAD_X
	box.content_margin_top = INVERT_PAD_Y
	box.content_margin_bottom = INVERT_PAD_Y

	var frame := PanelContainer.new()
	frame.add_theme_stylebox_override("panel", box)
	frame.size_flags_horizontal = Control.SIZE_SHRINK_END
	frame.add_child(ShellTheme.label(text, ShellTheme.T_BODY, ShellTheme.PAPER,
			ShellTheme.Weight.SEMIBOLD))
	return frame


## A struck time.
##
## `RichTextLabel` and its `[s]` tag, because `Label` cannot strike text at all.
## The control is configured as a static line: no scrolling, no wrapping, no
## shortcut keys, right-aligned by markup because `RichTextLabel` has no
## `horizontal_alignment`.
##
## **`fit_content` sizes the height and leaves the minimum width at zero** —
## measured, `get_combined_minimum_size().x` is 0.0 with it on. In this table that
## happens to be survivable, because the column is also occupied by plain `Label`
## times in the same face at the same size and the grid takes the widest, but it
## is survivable by accident. So the width is measured off the font and set
## explicitly, and the cell no longer depends on a valid lap sharing its column.
func _struck(text: String) -> Control:
	var cell := RichTextLabel.new()
	cell.bbcode_enabled = true
	cell.fit_content = true
	cell.scroll_active = false
	cell.shortcut_keys_enabled = false
	cell.autowrap_mode = TextServer.AUTOWRAP_OFF
	cell.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var face := ShellTheme.face()
	var size_px := int(ShellTheme.T_BODY)
	cell.add_theme_font_override("normal_font", face)
	cell.add_theme_font_size_override("normal_font_size", size_px)
	cell.add_theme_color_override("default_color", ShellTheme.PAPER_SOFT)
	cell.text = "[right][s]%s[/s][/right]" % text
	cell.custom_minimum_size = Vector2(
			ceilf(face.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1.0, size_px).x),
			0.0)
	return cell


## Fully transparent, built rather than written, because `shell_theme.gd` is the
## only file in the shell allowed to spell a color literal and the gate's check 9
## is a regex that enforces it.
static func _clear() -> Color:
	var transparent := ShellTheme.PAPER_INK
	transparent.a = 0.0
	return transparent


# --- the footer ------------------------------------------------------------------


## What the store did on the left, the office-use stamp on the right, and the way
## out. A results sheet with no way back to the paddock is a dead end reachable on
## the first session anybody finishes.
func _footer() -> Control:
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 16)

	_back = ShellTheme.row_button("Back to the paddock", ShellTheme.PAPER_INK)
	# `reset_to` and not `pop`: a session swapped the whole scene tree, so there is no
	# stack under this sheet to return through. `shell` is typed `Node`, so this call
	# returns a Variant and is deliberately discarded rather than assigned.
	_back.pressed.connect(func() -> void: shell.reset_to("paddock"))
	bar.add_child(_back)

	var said := ShellTheme.label(_footer_line, ShellTheme.T_FOOT, ShellTheme.PAPER_INK)
	said.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	said.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	bar.add_child(said)

	var stamp := ShellTheme.label(_stamp(), ShellTheme.T_FOOT, ShellTheme.PAPER_SOFT)
	stamp.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	bar.add_child(stamp)
	return bar


## The office-use corner. ADR-0041's `config_hash` is the whole session
## configuration — track file, layout, assists, tuning — in one token, so a bug
## report that carries it carries the setup. Omitted rather than faked when the
## result did not bring one.
func _stamp() -> String:
	var hash_hex := String(_result.get("config_hash", ""))
	return "" if hash_hex.is_empty() else "session %s" % hash_hex


# --- what the gate reads ------------------------------------------------------------


## The best-lap figure this sheet is displaying. Negative for none.
func pinned_best_s() -> float:
	return _pinned_best_s


## How many lap rows were actually drawn — counted while building, not recomputed
## from the input, so a row the table dropped is a row this does not claim.
func row_count() -> int:
	return _rows_drawn


func initial_focus() -> Control:
	return _back


# --- small helpers -------------------------------------------------------------------


static func _sectors_of(row: Dictionary) -> PackedFloat64Array:
	var carried: Variant = row.get("sectors", PackedFloat64Array())
	if carried is PackedFloat64Array:
		return carried
	# An `Array` of floats, which is what a Dictionary that has been through a JSON
	# round trip or a hand-written probe fixture carries.
	var out := PackedFloat64Array()
	if carried is Array:
		for value: Variant in carried:
			out.append(float(value))
	return out
