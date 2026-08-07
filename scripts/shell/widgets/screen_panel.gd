class_name ScreenPanel
extends Control

## The **screen** document family's page furniture: a `SCR_GROUND` field with any
## number of `SCR_CHROME` bars stacked under it, each ruled along its top edge.
## `docs/FRONTEND.md` §1 and mockup plates 4, 7 and 8.
##
## The bar-over-field arrangement is the whole shape of the screen family — boot,
## loading and pause are all one field and zero to two bars — so it lives here
## once instead of being three hand-built `VBoxContainer`s that drift apart. The
## rule along a bar's top edge is `ShellTheme.rule(false, false)`, which is 1 px of
## `SCR_RULE`; the 2 px `SCR_INK` weight is the *paper* titleblock's and does not
## appear in this family at all.
##
## `boot_screen.gd` predates this widget and builds its own bar by hand. It is not
## converted here because it is not this agent's file; the bodies are the same
## eight properties and the note is in the report.
##
## ## Use
##
##     var page := ScreenPanel.new()                 # opaque
##     var page := ScreenPanel.new(ShellTheme.VEIL_ALPHA)   # an overlay's veil
##     page.field().add_child(whatever_is_centered)
##     var bar := page.add_bar()                     # an HBoxContainer, ruled above
##     bar.add_child(ShellTheme.kicker(...))
##
## Every color comes from `ShellTheme`. `shell_probe.gd` check 9 is a regex over
## this directory and a hex literal here fails the gate.

## The field's inset, in pixels at the 16 px root the type scale is ported to.
## The plate's centered block sits in `padding: 1.5rem`, which is 24.
const FIELD_PAD := 24.0

## Between the things sharing one bar. The plate's bars are a `justify-content:
## space-between` flex; here the left child expands and this is the gap that
## remains when both sides are wide enough to touch.
const BAR_SEPARATION := 18.0

var _rows: VBoxContainer
var _field: MarginContainer


## `veil_alpha` is 1.0 for a screen that owns the display and
## `ShellTheme.VEIL_ALPHA` for an overlay that must leave the world visible behind
## it. There is no third value: the constant is the plate's, and an overlay that
## picked its own would be a second opinion about how opaque "paused" looks.
func _init(veil_alpha := 1.0) -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	# The furniture never eats a click; the controls inside it do.
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(ShellTheme.ground(false, veil_alpha))

	_rows = VBoxContainer.new()
	_rows.name = "Rows"
	_rows.set_anchors_preset(Control.PRESET_FULL_RECT)
	# Zero, because the bars supply their own edge rule and a container separation
	# would put a gap of ground between a rule and the chrome it belongs to.
	_rows.add_theme_constant_override("separation", 0)
	add_child(_rows)

	_field = MarginContainer.new()
	_field.name = "Field"
	_field.size_flags_vertical = Control.SIZE_EXPAND_FILL
	for side: String in ["left", "right", "top", "bottom"]:
		_field.add_theme_constant_override("margin_" + side, int(FIELD_PAD))
	_rows.add_child(_field)


## The expanding middle. Everything that is not a bar goes in here.
func field() -> MarginContainer:
	return _field


## Append a chrome bar below whatever is already there, ruled along its top edge.
## Returns the row to fill: its first child should carry
## `size_flags_horizontal = SIZE_EXPAND_FILL` if the rest is to sit hard right.
func add_bar() -> HBoxContainer:
	_rows.add_child(ShellTheme.rule(false, false))
	var bar := ShellTheme.panel(false)
	bar.name = "Bar"
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", int(BAR_SEPARATION))
	row.alignment = BoxContainer.ALIGNMENT_BEGIN
	bar.add_child(row)
	_rows.add_child(bar)
	return row


## A vertical 1 px `SCR_RULE`, for the seam between two columns of a field.
##
## Transposed from `ShellTheme.rule()` rather than given its own thickness, so the
## horizontal and vertical seams cannot drift to different weights — which is
## exactly the drift the theme's heavy/light distinction exists to prevent.
static func divider() -> ColorRect:
	var node := ShellTheme.rule(false, false)
	node.custom_minimum_size = Vector2(node.custom_minimum_size.y, 0.0)
	node.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	node.size_flags_vertical = Control.SIZE_EXPAND_FILL
	return node
