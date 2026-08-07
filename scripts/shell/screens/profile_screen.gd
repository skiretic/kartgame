extends ShellScreen

## Plate 10. The paper-document family, and the number panel is the hero.
##
## Three columns on one sheet: the panel, who the driver is, and what they have
## actually done. Everything on it comes off `KartProfile` — the one thing in this
## project that survives a quit — and nothing on it is computed for display that
## is not stored.
##
## ## The panel
##
## Art. 3.7 is **sourced and verified** in `docs/KART_SPEC.md` §60.4.1, read off
## `refs/frontend/fia_karting_technical_regulations_2026.pdf` pp. 5-6 rather than
## recalled: *"Racing numbers must be black, in an Arial font on a yellow
## background... at least 15 cm high and have a 2 cm thick stroke... bordered by a
## yellow background of at least 1 cm."* So three of the four things this panel
## draws are regulation and are drawn that way: black figures, an Arial-metric
## face (Liberation Sans is metric-compatible), and a clear yellow border wider
## than the 1-in-15 minimum the cap height implies.
##
## **The fourth is not.** `ShellTheme.PANEL_YELLOW` is `estimated` — a read off
## reference photographs F3/F4, marked provisional at its own declaration — and
## Art. 3.7 says "yellow" and no more. It is not upgraded to a citation here and it
## must not be upgraded anywhere else.
##
## ## The flag is deferred
##
## ADR-0053 §5 defers flag generation to issue **#187**. Nationality renders as its
## FIA three-letter code, which is what `KartProfile` stores. No emoji stands in
## for it: an emoji flag is a font's opinion about a country, it is absent from
## Liberation Sans entirely, and a tofu box where a flag should be is worse than
## the code that is actually in the save file.

## `user://tuning/`, from `scripts/ui/tuning_panel.gd:69`, which owns the preset
## format. Read rather than counted from a constant of our own, so a format change
## is one edit — but the *path* has to be spelled here because that file is a
## driving overlay and loading it to read two constants would pull the tuning
## registry into a menu.
const PRESET_DIR := "user://tuning"
const PRESET_EXTENSION := ".tuning"

## Arial's cap height as a fraction of the em, from `docs/KART_SPEC.md` §60.4.3's
## derivation (`em = 150 / 0.716`). `derived` from that, not measured here.
const ARIAL_CAP_EM := 0.716

## Art. 3.7's clear border, as a fraction of the cap height: *"bordered by a yellow
## background of at least 1 cm"* against the short-circuit *"at least 15 cm high"*.
## A minimum, so the plate's own padding is used whenever it is larger — which at
## `T_PANEL` it is, by a factor of five.
const PANEL_BORDER_PER_CAP := 10.0 / 150.0

## The plate's panel padding, in pixels at the 16 px root: `0.9rem 1.3rem`.
const PANEL_PAD_X := 21.0
const PANEL_PAD_Y := 14.0

## The plate's `border-radius: 3px`.
const PANEL_RADIUS := 3

var _back: Button


func build() -> void:
	var profile: KartProfile = shell.profile() if shell != null else null

	var sheet := PaperPanel.new("Profile", _heading(profile),
			"Everything that survives a quit")
	sheet.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(sheet)

	# `expand` false: the panel column is exactly as wide as the panel. A stretched
	# number plate is the one thing on this sheet that would be visibly wrong.
	_build_panel(sheet.add_column(false), profile)
	_build_identity(sheet.add_column(), profile)
	_build_bests(sheet.add_column(), profile)

	sheet.set_footer(
		"Name renders \"Surname, Forename\" everywhere the entry list does.",
		_stamp(profile))


## The titleblock's heading. Two authored strings and not one with a hole in it —
## ADR-0044 rule 1 — because "No profile" is a different fact from a driver's name
## and not a missing value inside the same sentence.
func _heading(profile: KartProfile) -> String:
	if profile == null:
		return "No profile — the extension is not built"
	var name := profile.get_driver_name()
	return name if not name.is_empty() else "No entry filed"


## The right-hand footer stamp. `is_save_blocked()` is surfaced here rather than
## swallowed: a profile that cannot be written is a career that is silently not
## being recorded, and the sheet is where somebody would look for that.
func _stamp(profile: KartProfile) -> String:
	if profile == null:
		return "nothing can be saved"
	if profile.is_save_blocked():
		return "SAVE BLOCKED — %s" % profile.save_block_reason()
	return "profile.save · atomic, F_FULLFSYNC'd"


# --- column 1: the panel -----------------------------------------------------------


func _build_panel(column: VBoxContainer, profile: KartProfile) -> void:
	column.alignment = BoxContainer.ALIGNMENT_CENTER

	var figure := "—"
	if profile != null and profile.get_driver_number() > 0:
		figure = str(profile.get_driver_number())

	var plate := PanelContainer.new()
	plate.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	var box := StyleBoxFlat.new()
	box.bg_color = ShellTheme.PANEL_YELLOW
	# Art. 3.7's minimum clear border, in pixels at this cap height, against the
	# plate's own padding. The plate wins here (14 px against 2.6) and the maxf is
	# what keeps that true if `T_PANEL` ever grows: at 54 px the cap is 38.7 px and
	# the regulation minimum is 38.7 / 15 = 2.58 px.
	var cap_px := ShellTheme.T_PANEL * ARIAL_CAP_EM
	var minimum := cap_px * PANEL_BORDER_PER_CAP
	box.content_margin_left = maxf(PANEL_PAD_X, minimum)
	box.content_margin_right = maxf(PANEL_PAD_X, minimum)
	box.content_margin_top = maxf(PANEL_PAD_Y, minimum)
	box.content_margin_bottom = maxf(PANEL_PAD_Y, minimum)
	box.corner_radius_top_left = PANEL_RADIUS
	box.corner_radius_top_right = PANEL_RADIUS
	box.corner_radius_bottom_left = PANEL_RADIUS
	box.corner_radius_bottom_right = PANEL_RADIUS
	plate.add_theme_stylebox_override("panel", box)
	column.add_child(plate)

	# Bold, because §60.4.3 measured it: Arial Regular's digit stem at a 150 mm cap
	# is ~19.5 mm against the regulation's 20 mm stroke, which is the marginal case.
	# Bold is ~27 mm and is the safe spec.
	var digits := ShellTheme.label(figure, ShellTheme.T_PANEL, ShellTheme.PANEL_FIGURE,
			ShellTheme.Weight.BOLD)
	digits.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	plate.add_child(digits)

	var caption := ShellTheme.kicker("regulation proportion · Art. 3.7",
			ShellTheme.PAPER_SOFT)
	caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	column.add_child(caption)


# --- column 2: who ------------------------------------------------------------------


func _build_identity(column: VBoxContainer, profile: KartProfile) -> void:
	if profile == null:
		column.add_child(_note("There is no profile to read. Build the extension "
				+ "with `scons target=editor arch=arm64` and relaunch."))
		return

	var name := profile.get_driver_name()
	var surname := name
	var forename := ""
	# `KartProfile` stores one string and `src/core/profile.h`'s own example is
	# `driver_name Skirving, Anthony`. The split is on the stored separator; a name
	# without one is a surname, which is how a single-name entry appears on an FIA
	# entry list.
	var comma := name.find(",")
	if comma >= 0:
		surname = name.substr(0, comma).strip_edges()
		forename = name.substr(comma + 1).strip_edges()

	_row(column, "Surname", surname if not surname.is_empty() else "—", true)
	_row(column, "Forename", forename if not forename.is_empty() else "—")
	# The three-letter FIA code, not a flag. ADR-0053 §5 defers the flag to #187.
	_row(column, "Nationality", _or_dash(profile.get_nationality()))
	_row(column, "Number", "—" if profile.get_driver_number() <= 0
			else str(profile.get_driver_number()))
	# The slug is filename-safe by construction, so the only thing between it and a
	# readable label is the underscore it was made safe with.
	_row(column, "Livery", _or_dash(profile.get_livery().replace("_", " ")))


# --- column 3: what has been done -------------------------------------------------


func _build_bests(column: VBoxContainer, profile: KartProfile) -> void:
	column.add_child(ShellTheme.kicker("bests · per track, per layout, per class",
			ShellTheme.PAPER_SOFT))

	if profile == null:
		column.add_child(_note("No bests. Nothing has been able to save one."))
	elif profile.best_count() == 0:
		# Two authored sentences, per ADR-0044 rule 1 and the paddock's own
		# first-run card.
		column.add_child(_note("No best set anywhere yet."))
		column.add_child(_note("The sheet is blank on purpose — it fills in the "
				+ "first time a clean lap closes."))
	else:
		for row: Dictionary in profile.bests_table():
			column.add_child(_best_line(row))

	column.add_child(PaperPanel.interior_rule())
	column.add_child(ShellTheme.kicker("tuning presets", ShellTheme.PAPER_SOFT))
	column.add_child(_note(_presets_line()))

	# The one focusable control on the sheet.
	#
	# `boot_screen.gd`'s reasoning, and it applies unchanged: `shell_probe.gd`
	# check 4 wants focus to land on something with a rect inside the viewport, and
	# a document with no affordance has nowhere to put it. It is also the honest pad
	# answer — Circle works here for the same reason it works everywhere else, and a
	# screen whose only exit is an unlabelled button is the `look_back` failure with
	# a menu on top of it.
	column.add_child(_spacer(8))
	_back = ShellTheme.row_button("Back to the paddock", ShellTheme.PAPER_INK)
	_back.pressed.connect(func() -> void:
		if stack != null:
			stack.call("back"))
	column.add_child(_back)


## One stored best. Indexed with `[]` and not `get(key, default)`: these five keys
## are `KartProfile::best_at`'s contract, and a default here would draw a plausible
## row forever after a rename. `paddock_screen.gd` reads `track_id` off this same
## dictionary and there is no such key — see the report.
func _best_line(row: Dictionary) -> Control:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 10)

	# The class uppercased and the layout not: `kart_class_name` returns the slug
	# `kz2` and the plate writes it KZ2, which is how the class is spelled
	# everywhere a person reads it; `layout_name` returns `forward`, which is a word.
	var where := ShellTheme.label("%s %s %s" % [
		String(row["track"]),
		KartSession.layout_name(int(row["layout"])),
		KartSession.kart_class_name(int(row["kart_class"])).to_upper(),
	], ShellTheme.T_BODY, ShellTheme.PAPER_SOFT)
	where.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	line.add_child(where)

	line.add_child(ShellTheme.label(
			SessionRunner.format_time(float(row["lap_time_s"])),
			ShellTheme.T_BODY, ShellTheme.PAPER_INK, ShellTheme.Weight.SEMIBOLD))
	return line


## How many presets are saved, and where. Counted off disk rather than stored,
## because `KartTuning.save_preset` writes the directory and nothing else tracks it.
func _presets_line() -> String:
	if not DirAccess.dir_exists_absolute(PRESET_DIR):
		return "None saved. %s does not exist yet." % PRESET_DIR
	var count := 0
	for entry: String in DirAccess.get_files_at(PRESET_DIR):
		if entry.ends_with(PRESET_EXTENSION):
			count += 1
	if count == 0:
		return "None saved. %s is empty." % PRESET_DIR
	return "%d saved · %s" % [count, PRESET_DIR]


# --- small parts -------------------------------------------------------------------


## A label/value row in the plate's `.ptab` shape: soft label left, ink value hard
## right, tabular figures throughout because `ShellTheme.face` sets `tnum`.
func _row(column: VBoxContainer, label_text: String, value: String,
		emphatic := false) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)

	var left := ShellTheme.label(label_text, ShellTheme.T_BODY, ShellTheme.PAPER_SOFT)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(left)

	row.add_child(ShellTheme.label(value, ShellTheme.T_BODY, ShellTheme.PAPER_INK,
			ShellTheme.Weight.BOLD if emphatic else ShellTheme.Weight.REGULAR))
	column.add_child(row)


func _note(text: String) -> Label:
	var node := ShellTheme.label(text, ShellTheme.T_BODY, ShellTheme.PAPER_SOFT)
	node.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	node.custom_minimum_size = Vector2(240.0, 0.0)
	return node


static func _or_dash(text: String) -> String:
	return text if not text.strip_edges().is_empty() else "—"


static func _spacer(height: int) -> Control:
	var node := Control.new()
	node.custom_minimum_size = Vector2(0.0, float(height))
	return node


# --- ShellScreen ---------------------------------------------------------------------


func initial_focus() -> Control:
	return _back


func title() -> String:
	return "profile"
