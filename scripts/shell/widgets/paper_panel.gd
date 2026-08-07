class_name PaperPanel
extends PanelContainer

## The **paper** document family's sheet: a titleblock over a 2 px `PAPER_INK`
## rule, a body of columns separated by 1 px `PAPER_RULE` seams, and a footer
## above another 2 px `PAPER_INK` rule. `docs/FRONTEND.md` §1 and mockup plates 2,
## 3, 6, 9 and 10.
##
## ## Why the rule weights are in the widget and not in each screen
##
## **2 px in the ink for a titleblock or a footer, 1 px in the rule inside the
## body.** That difference is most of what makes a sheet read as a federation
## document rather than as a table, and it is one line of CSS on the plate — which
## is exactly the kind of thing five screens each reimplement slightly differently.
## `ShellTheme.rule(heavy, paper_family)` already owns the two weights; this owns
## *where each one goes*, so a screen never chooses.
##
## ## Use
##
##     var sheet := PaperPanel.new("Profile", "Skirving, Anthony",
##             "Everything that survives a quit")
##     var left := sheet.add_column()      # first column, no seam
##     var right := sheet.add_column()     # seam drawn automatically
##     sheet.set_footer("Name renders \"Surname, Forename\".", "profile.save")
##
## `add_column()` inserts the seam *between* columns, so a screen never draws one
## and never has to remember not to draw a trailing one — the plate's own
## `border-right` on every sheet but the last is the bug this replaces.
##
## Every color comes from `ShellTheme`. `shell_probe.gd` check 9 is a regex over
## this directory and a hex literal here fails the gate.

## The plate's paddings, in pixels at the 16 px root the type scale is ported to,
## rounded to the nearest pixel: titleblock `1.1rem 1.4rem 0.9rem`, sheet
## `1rem 1.4rem 1.3rem`, footer the same horizontal inset.
##
## Rounded to nearest rather than up. `ShellTheme`'s type sizes round *up* because
## a 9.3 px column head is unreadable and this project has already shipped a HUD at
## 60% of its designed size once; a half-pixel of padding has no such failure mode.
const EDGE_X := 22.0      # 1.4rem
const HEAD_TOP := 18.0    # 1.1rem
const HEAD_BOTTOM := 14.0 # 0.9rem
const BODY_TOP := 16.0    # 1rem
const BODY_BOTTOM := 21.0 # 1.3rem
const FOOT_Y := 12.0      # 0.75rem, between the two footer rules

## Between the doc kicker and the heading, and between rows of a column.
const STACK_SEPARATION := 4.0

var _column: VBoxContainer
var _body: HBoxContainer
var _foot: HBoxContainer
var _columns := 0


## `doc` is the uppercase kicker — "Profile", "Classification" — and `heading` is
## the sentence under it. `meta` is the right-aligned note; empty omits it.
##
## `shadowed` is for a sheet floating over the 3D backdrop, as the paddock's two
## cards do. A full-bleed sheet is not shadowed: it has no edge to cast one from.
func _init(doc: String, heading: String, meta := "", shadowed := false) -> void:
	# The ground, the shadow and the family's paper color all come from
	# `ShellTheme.panel()`; only its content margins are dropped, because the
	# plate's rules run edge to edge and an 18 px container margin insets them.
	var source := ShellTheme.panel(true, shadowed)
	var box := source.get_theme_stylebox("panel") as StyleBoxFlat
	source.free()
	for side: String in ["left", "right", "top", "bottom"]:
		box.set("content_margin_" + side, 0.0)
	add_theme_stylebox_override("panel", box)

	_column = VBoxContainer.new()
	_column.name = "Sheet"
	_column.add_theme_constant_override("separation", 0)
	add_child(_column)

	_column.add_child(_titleblock(doc, heading, meta))
	_column.add_child(ShellTheme.rule(true, true))

	_body = HBoxContainer.new()
	_body.name = "Body"
	# Zero: the seam between two columns is a `divider()`, and a container
	# separation on top of it would put paper on both sides of a hairline.
	_body.add_theme_constant_override("separation", 0)
	_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_column.add_child(_body)


## The plate's `.titleblock`: an uppercase doc kicker over a bold heading on the
## left, a right-aligned meta note on the right, and the 2 px rule under both.
##
## The meta is `T_FOOT` and soft rather than `T_COLHEAD` and tracked: it is a
## sentence, not a label, and uppercase tracking at 10 px turns a sentence into a
## row of stamps.
func _titleblock(doc: String, heading: String, meta: String) -> Control:
	var pad := MarginContainer.new()
	pad.name = "Titleblock"
	pad.add_theme_constant_override("margin_left", int(EDGE_X))
	pad.add_theme_constant_override("margin_right", int(EDGE_X))
	pad.add_theme_constant_override("margin_top", int(HEAD_TOP))
	pad.add_theme_constant_override("margin_bottom", int(HEAD_BOTTOM))

	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", int(EDGE_X))
	pad.add_child(row)

	var stack := VBoxContainer.new()
	stack.add_theme_constant_override("separation", int(STACK_SEPARATION))
	stack.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	stack.add_child(ShellTheme.kicker(doc, ShellTheme.PAPER_SOFT))
	stack.add_child(ShellTheme.label(heading, ShellTheme.T_TITLE, ShellTheme.PAPER_INK,
			ShellTheme.Weight.BOLD))
	row.add_child(stack)

	if meta.is_empty():
		return pad
	var note := ShellTheme.label(meta, ShellTheme.T_FOOT, ShellTheme.PAPER_SOFT)
	note.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	note.size_flags_vertical = Control.SIZE_SHRINK_END
	row.add_child(note)
	return pad


## A body column, with the `PAPER_RULE` seam drawn before every one after the
## first. `expand` false gives a column exactly as wide as its contents — the
## profile sheet's number panel, which must not stretch.
func add_column(expand := true) -> VBoxContainer:
	if _columns > 0:
		_body.add_child(divider())
	_columns += 1

	var pad := MarginContainer.new()
	pad.name = "Column%d" % _columns
	pad.add_theme_constant_override("margin_left", int(EDGE_X))
	pad.add_theme_constant_override("margin_right", int(EDGE_X))
	pad.add_theme_constant_override("margin_top", int(BODY_TOP))
	pad.add_theme_constant_override("margin_bottom", int(BODY_BOTTOM))
	pad.size_flags_horizontal = Control.SIZE_EXPAND_FILL if expand \
			else Control.SIZE_SHRINK_BEGIN
	pad.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_body.add_child(pad)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", int(STACK_SEPARATION))
	pad.add_child(column)
	return column


## The footer, above its own 2 px rule. `stamp` is the right-aligned half; empty
## omits it. Calling this twice replaces the text rather than stacking a second
## footer, so a screen may fill it after its body is built.
func set_footer(text: String, stamp := "") -> void:
	if _foot == null:
		_column.add_child(ShellTheme.rule(true, true))
		var pad := MarginContainer.new()
		pad.name = "Footer"
		pad.add_theme_constant_override("margin_left", int(EDGE_X))
		pad.add_theme_constant_override("margin_right", int(EDGE_X))
		pad.add_theme_constant_override("margin_top", int(FOOT_Y))
		pad.add_theme_constant_override("margin_bottom", int(FOOT_Y))
		_column.add_child(pad)
		_foot = HBoxContainer.new()
		_foot.add_theme_constant_override("separation", int(EDGE_X))
		pad.add_child(_foot)
	for child: Node in _foot.get_children():
		_foot.remove_child(child)
		child.queue_free()

	var left := ShellTheme.label(text, ShellTheme.T_FOOT, ShellTheme.PAPER_SOFT)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_foot.add_child(left)
	if stamp.is_empty():
		return
	_foot.add_child(ShellTheme.kicker(stamp, ShellTheme.PAPER_SOFT))


## The body, for a screen that wants to place its own children rather than take
## the padded columns. Rare; the profile sheet does not use it.
func body() -> HBoxContainer:
	return _body


## A horizontal 1 px `PAPER_RULE` inside a column — between a table and the note
## under it. The heavy weight is the titleblock's and the footer's, and this widget
## has already placed both of those.
static func interior_rule() -> ColorRect:
	return ShellTheme.rule(false, true)


## A vertical 1 px `PAPER_RULE`, the seam between two columns.
##
## Transposed from `ShellTheme.rule()` rather than given its own thickness, so the
## horizontal and vertical seams cannot drift to different weights.
static func divider() -> ColorRect:
	var node := ShellTheme.rule(false, true)
	node.custom_minimum_size = Vector2(node.custom_minimum_size.y, 0.0)
	node.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	node.size_flags_vertical = Control.SIZE_EXPAND_FILL
	return node
