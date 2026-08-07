class_name ShellTheme
extends RefCounted

## The shell's colors, faces and type scale. **The only file under
## `scripts/shell/` permitted to write a hex literal**, and
## `tools/verify/shell_probe.gd` check 9 is a regex that enforces it.
##
## Every token is read from `docs/mockups/frontend_family.html`, which is the
## approved plate, so a screen that wants a color asks for one by name and the
## whole shell moves together when the livery round picks a real accent hue.
##
## ## Two document families, and they are not skins of each other
##
## `docs/FRONTEND.md` §1: a live timing screen is a **screen** — dark, backlit,
## read at a glance at speed — and a classification is **paper** — a federation
## document, read at rest, printable. They share a type scale and nothing else.
## Mixing them is the tell that a UI was themed rather than designed, so the
## families are separate constant blocks here and a screen picks one.
##
## `scripts/ui/driving_hud.gd` is deliberately in neither. It is a positive-LCD
## instrument built off a photograph and FRONTEND §1 names it as the one
## exception; do not theme it.
##
## ## Why this is code and not a `Theme.tres`
##
## `assets/fonts/` is gitignored (`.gitignore:13`, and `git ls-files assets/fonts`
## returns nothing) because the TTFs are reproduced by
## `tools/assets/fetch_liberation_sans.sh` from a pinned checksum. A `.tres` with
## an `ext_resource` pointing into it does not load on a fresh clone — it fails
## the whole resource, not just the font — which is the same trap
## `valdirone.tscn` and `kartview.tscn` already carry headers about for `.glb`
## and `.lmbake`. So the theme is built in code and the font is loaded behind
## `ResourceLoader.exists()`, with the fallback **reported rather than hidden**:
## `font_source()` is printed by the gate, because a still shot on the wrong face
## has to be diagnosable from its own log.
##
## `Theme.set_type_variation` is avoided outright for a second reason: given a
## base that is not a real class name it fails **silently**, producing an
## unthemed control rather than an error. The helpers below configure controls
## directly, so a typo is a missing method and not a gray button.


# --- the screen-document family --------------------------------------------

const SCR_GROUND := Color(0.062745, 0.094118, 0.125490)  # #101820
const SCR_CHROME := Color(0.086275, 0.149020, 0.227451)  # #16263a
const SCR_INK := Color(0.862745, 0.901961, 0.941176)     # #dce6f0
const SCR_SOFT := Color(0.498039, 0.556863, 0.627451)    # #7f8ea0
const SCR_RULE := Color(0.137255, 0.203922, 0.290196)    # #23344a

# --- the paper-document family ---------------------------------------------

const PAPER := Color(0.964706, 0.956863, 0.937255)       # #f6f4ef
const PAPER_INK := Color(0.090196, 0.094118, 0.101961)   # #17181a
const PAPER_SOFT := Color(0.384314, 0.396078, 0.415686)  # #62656a
const PAPER_RULE := Color(0.831373, 0.819608, 0.784314)  # #d4d1c8
const PAPER_BAND := Color(0.925490, 0.921569, 0.890196)  # #ecebe3

## Placeholder until the livery round picks the hue — FRONTEND.md says so in as
## many words, and it is one constant so that round is one edit.
const ACCENT := Color(0.290196, 0.658824, 1.0)           # #4aa8ff

# --- states, and they are never decoration ---------------------------------
#
# A color here means a fact about a lap time. Nothing is tinted for looks; a
# green cell says the lap improved and a red one says it did not, and neither
# appears anywhere that is not reporting exactly that.

const ST_IMPROVE := Color(0.184314, 0.749020, 0.372549)  # #2fbf5f
const ST_SLOWER := Color(0.878431, 0.270588, 0.270588)   # #e04545
const ST_BEST := Color(0.690196, 0.427451, 0.878431)     # #b06de0

## Art. 3.7's number panel, sampled from references F3/F4. Provisional, and
## marked so: it is a **read off a photograph**, not a sourced value, and
## CLAUDE.md's three-way rule says an estimate says which it is.
const PANEL_YELLOW := Color(0.843137, 0.764706, 0.329412)  # #d7c354
const PANEL_FIGURE := Color(0.0, 0.0, 0.0)

## The pause veil. ADR-0052 and mockup plate 8: the world stays visible behind
## it, so this is `SCR_GROUND` at the plate's own alpha rather than an opaque
## panel. The kart is not frozen and the screen must not claim it is.
const VEIL_ALPHA := 0.88

## A selected row is marked by an accent left border, and a focused row by a
## faint accent wash — never by a filled bar. The plate uses 0.09-0.10 on both
## families; one number, because the eye reads the same weight on either ground.
const ROW_WASH_ALPHA := 0.10
const SELECT_BORDER_PX := 3.0


# --- type -------------------------------------------------------------------
#
# The mockup is CSS at a 15 px root; these are its rem values ported to a 16 px
# root, which is where the quoted pixel sizes come from. Rounding up rather than
# down is deliberate and this project has already shipped the other choice once:
# `project.godot`'s [display] comment is the receipt for a HUD drawn at 60% of
# its designed size and reported as unreadable. At the mockup's own 15 px root a
# 0.62rem column head renders at 9.3 px.

const T_COLHEAD := 10.0   # 0.62rem — column heads, doc kickers, state chips
const T_FOOT := 11.0      # 0.68rem — footers, the boot check line
const T_BODY := 13.0      # 0.80rem — table bodies, menu rows
const T_ROW := 14.0       # 0.85rem — the denser forms (setup, settings)
const T_TITLE := 17.0     # 1.05rem — titleblock headings, the session clock
const T_HERO := 30.0      # 1.90rem — the results best-lap figure
const T_PANEL := 54.0     # 3.40rem — the profile number panel
const T_WORDMARK := 42.0  # boot, between the plate's clamp(1.8rem, 5vw, 2.6rem)

## Letter-spacing, in whole pixels, because `FontVariation.spacing_glyph` is an
## integer advance and not an em fraction. The plate's uppercase label styles run
## 0.12-0.16em and the boot wordmark runs 0.28em; at the sizes above that is +1
## and +12 respectively. Computed rather than typed, so changing a size moves its
## tracking with it.
const TRACK_LABEL_EM := 0.13
const TRACK_WORDMARK_EM := 0.28


## The canvas the plate was drawn against.
##
## **900, not 1080, and the arithmetic is the argument.** `project.godot` sets
## `window/stretch/mode="canvas_items"` with `aspect="expand"`, which pins the
## viewport's *height* at `viewport_height` and lets the width grow — so
## `get_viewport_rect().size.y` is 900 in the shipped game at every window size,
## and this divisor is 1.0. Dividing by 1080 instead would multiply the whole
## shell by 0.833 and render the 10 px column heads at 8.3, which is the bug the
## [display] comment records.
##
## `driving_hud.gd` and `timing_hud.gd` do divide by 1080, and that is left
## alone: they were tuned by eye at the resulting size and "fixing" the divisor
## would silently resize a HUD nobody asked to change. The two conventions
## coexist on purpose and this comment is the note saying so.
const DESIGN_HEIGHT := 900.0

## Scale by height, never width. An ultrawide window scaled by width puts a
## timing tower a third of the way across the screen.
static func scale_for(viewport_size: Vector2) -> float:
	return maxf(0.5, viewport_size.y / DESIGN_HEIGHT)


# --- faces ------------------------------------------------------------------

const FONT_DIR := "res://assets/fonts/liberation/"
const FONT_REGULAR := FONT_DIR + "LiberationSans-Regular.ttf"
const FONT_BOLD := FONT_DIR + "LiberationSans-Bold.ttf"

enum Weight { REGULAR, SEMIBOLD, BOLD }

## Liberation Sans ships Regular and Bold and the mockup uses five weights
## (400/600/650/700/800). The 600 and 650 styles become a synthesized semibold
## off the regular face; 700 and 800 both become Bold, because faking a heavier
## bold on top of a real one smears at 10 px and the plate's 800 is only ever
## used at 30 px and above where Bold already reads as heavy.
const EMBOLDEN_SEMIBOLD := 0.35

static var _faces: Dictionary = {}
static var _source := ""


## `Weight` and a whole-pixel tracking, as a cached `FontVariation`.
##
## Cached because a `FontVariation` carries its own glyph cache and building one
## per label per frame would rasterize the alphabet again every time a table
## redraws.
static func face(weight: int = Weight.REGULAR, tracking_px: int = 0) -> Font:
	var key := "%d/%d" % [weight, tracking_px]
	if _faces.has(key):
		return _faces[key]

	var variation := FontVariation.new()
	variation.base_font = _base(weight)
	if weight == Weight.SEMIBOLD:
		variation.variation_embolden = EMBOLDEN_SEMIBOLD
	if tracking_px != 0:
		variation.spacing_glyph = tracking_px
	# **`kern: 0` is the line that actually delivers tabular figures. `tnum` is a
	# no-op here and shipping it alone was wrong.**
	#
	# Measured on Liberation Sans Bold at 30 px: every digit already has a 17.000
	# px advance with or without `tnum` — the face carries no `tnum` table, so the
	# feature does nothing — and yet `1:11.111` renders 141.00 px against
	# `0:00.000`'s 146.00. Five pixels, 3.4% of the column, on a screen whose whole
	# job is a column of lap times that line up.
	#
	# The cause is **kerning between digit pairs**, which `tnum` does not touch and
	# which the Arial-metric argument does not predict. Disabling it flattens all
	# four samples to exactly 118.00 px. The engine's own fallback face measures a
	# uniform 147.00, so the shipped font was worse than the fallback at the one
	# thing FRONTEND §3 calls non-negotiable.
	#
	# `tnum` stays because it is free and correct for any face that does carry the
	# table. If a column ever wobbles anyway, the proven fallback is
	# `timing_hud.gd:361`'s hand-pitched `_digits()`.
	variation.opentype_features = {_tag("tnum"): 1, _tag("kern"): 0}
	_faces[key] = variation
	return variation


## The tracking for a label style, in whole pixels at a given size.
static func tracking(size_px: float, em: float) -> int:
	return int(roundf(size_px * em))


## What the faces actually resolved to: a path, or `fallback` when the gitignored
## TTFs are not on disk. Printed by the gate — a fresh clone rendering in Godot's
## default sans is not a failure, but it must never be a silent one.
static func font_source() -> String:
	if _source.is_empty():
		_base(Weight.REGULAR)
	return _source


static func _base(weight: int) -> Font:
	var path := FONT_BOLD if weight == Weight.BOLD else FONT_REGULAR
	if ResourceLoader.exists(path):
		_source = FONT_DIR
		var loaded := ResourceLoader.load(path)
		if loaded is Font:
			return loaded
	_source = "fallback (%s absent -- run tools/assets/fetch_liberation_sans.sh)" % FONT_DIR
	return ThemeDB.fallback_font


static func _tag(name: String) -> int:
	return TextServerManager.get_primary_interface().name_to_tag(name)


# --- helpers ----------------------------------------------------------------
#
# Screens build `Label`s and `Button`s through these rather than setting theme
# overrides by hand, so "column head" is one decision in one place instead of
# four properties repeated at every call site.


static func label(text: String, size_px: float, color: Color,
		weight: int = Weight.REGULAR, tracking_em := 0.0) -> Label:
	var node := Label.new()
	node.text = text
	node.add_theme_font_override("font", face(weight, tracking(size_px, tracking_em)))
	node.add_theme_font_size_override("font_size", int(roundf(size_px)))
	node.add_theme_color_override("font_color", color)
	return node


## An uppercase kicker: the plate's column heads, doc labels and state chips are
## all one style, and they are all `letter-spacing: 0.12-0.16em; uppercase`.
static func kicker(text: String, color: Color, size_px := T_COLHEAD) -> Label:
	return label(text.to_upper(), size_px, color, Weight.SEMIBOLD, TRACK_LABEL_EM)


## A menu row. Focusable, flat, and marked by an accent left border when it holds
## focus — never by a filled bar, per the plate.
##
## `FOCUS_ALL` is set explicitly even though `Button` defaults to it, because
## `shell_probe.gd` check 5 walks exactly the set of `FOCUS_ALL` descendants and
## a row that opts out of focus by accident is a row a pad cannot reach.
static func row_button(text: String, family_ink: Color, size_px := T_BODY) -> Button:
	var node := Button.new()
	node.text = text
	node.focus_mode = Control.FOCUS_ALL
	node.alignment = HORIZONTAL_ALIGNMENT_LEFT
	node.add_theme_font_override("font", face(Weight.SEMIBOLD))
	node.add_theme_font_size_override("font_size", int(roundf(size_px)))
	for state: String in ["font_color", "font_hover_color", "font_focus_color",
			"font_pressed_color"]:
		node.add_theme_color_override(state, family_ink)
	node.add_theme_color_override("font_disabled_color",
			Color(family_ink, 0.45))
	node.add_theme_stylebox_override("normal", _row_box(false))
	node.add_theme_stylebox_override("hover", _row_box(true))
	node.add_theme_stylebox_override("focus", _row_box(true))
	node.add_theme_stylebox_override("pressed", _row_box(true))
	node.add_theme_stylebox_override("disabled", _row_box(false))
	return node


static func _row_box(selected: bool) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = Color(ACCENT, ROW_WASH_ALPHA) if selected else Color(0, 0, 0, 0)
	box.border_color = ACCENT
	box.border_width_left = int(SELECT_BORDER_PX) if selected else 0
	box.content_margin_left = 8.0
	box.content_margin_right = 8.0
	box.content_margin_top = 4.0
	box.content_margin_bottom = 4.0
	return box


## A filled panel in one of the two families. `paper` picks the family; the
## shadow is the plate's, and only the paper cards over the 3D backdrop carry one
## — a screen-family panel sits on its own ground and casts nothing.
static func panel(paper_family: bool, shadowed := false) -> PanelContainer:
	var node := PanelContainer.new()
	var box := StyleBoxFlat.new()
	box.bg_color = PAPER if paper_family else SCR_CHROME
	box.content_margin_left = 18.0
	box.content_margin_right = 18.0
	box.content_margin_top = 14.0
	box.content_margin_bottom = 14.0
	if shadowed:
		box.shadow_color = Color(0, 0, 0, 0.4)
		box.shadow_size = 12
		box.shadow_offset = Vector2(0, 6)
	node.add_theme_stylebox_override("panel", box)
	return node


## A horizontal rule. **2 px in the family ink for a titleblock or a footer, 1 px
## in the family rule inside the body** — that weight difference is most of what
## makes a sheet read as a federation document rather than as a table, so it is a
## parameter here rather than four hand-set `custom_minimum_size`s.
static func rule(heavy: bool, paper_family: bool) -> ColorRect:
	var node := ColorRect.new()
	node.color = (PAPER_INK if heavy else PAPER_RULE) if paper_family \
			else (SCR_INK if heavy else SCR_RULE)
	node.custom_minimum_size = Vector2(0.0, 2.0 if heavy else 1.0)
	node.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return node


## The full-bleed ground for a screen in either family.
static func ground(paper_family: bool, alpha := 1.0) -> ColorRect:
	var node := ColorRect.new()
	node.color = Color(PAPER if paper_family else SCR_GROUND, alpha)
	node.set_anchors_preset(Control.PRESET_FULL_RECT)
	node.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return node
