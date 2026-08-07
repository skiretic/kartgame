class_name ScreenStack
extends Control

## Push, pop, focus, and the back rule. ADR-0053 §1 and §2.
##
## ## Why menu input is read in `_input()` and not `_unhandled_input()`
##
## This was measured, not assumed, and the measurement changed the design.
## Godot's built-in `ui_*` actions are **already bound to the same events** the
## six `menu_*` actions carry:
##
##     ui_up      Up,    pad 11 (d-pad up),    left stick Y-
##     ui_down    Down,  pad 12,               left stick Y+
##     ui_left    Left,  pad 13,               left stick X-
##     ui_right   Right, pad 14,               left stick X+
##     ui_accept  Enter, KP Enter, Space       -- but NOT Cross
##     ui_cancel  Escape                       -- but NOT Circle
##
## The engine walks focus on `ui_*` inside the `Viewport`'s GUI pass, which runs
## **after** `_input()` and **before** `_unhandled_input()`. So a stack that read
## its directions in `_unhandled_input` would move the selection twice on every
## press — once by the engine, once by itself — and the second move is invisible
## in the code that causes it.
##
## Reading them in `_input()` and consuming them puts one walker in charge. It
## also makes the `menu_*` actions genuinely load-bearing rather than a second
## name for something the engine does anyway, which matters: ADR-0053 §2 decided
## menus get their own `[input]` entries precisely so the two contexts are
## separable, and a declared action nothing reads is the `look_back` failure
## CLAUDE.md already has a paragraph about.
##
## The two actions that are *not* redundant are the reason any of this is
## needed: **Cross and Circle**, which the engine's defaults do not carry, and
## which ADR-0053 §2 makes the PlayStation-standard confirm/back pair.
##
## ## Repeat
##
## Held directions repeat from a clock here rather than from OS key-repeat, so a
## thumb on the d-pad, a thumb on the stick and a finger on the arrow key all
## scroll at the same rate. Echo events are consumed and ignored for the same
## reason.

const REPEAT_DELAY_S := 0.42
const REPEAT_RATE_S := 0.11

const DIRECTIONS := {
	&"menu_up": SIDE_TOP,
	&"menu_down": SIDE_BOTTOM,
	&"menu_left": SIDE_LEFT,
	&"menu_right": SIDE_RIGHT,
}

signal screen_pushed(screen: ShellScreen)
signal screen_popped(screen: ShellScreen)

## The `ShellRoot`, handed to every screen as `screen.shell`.
##
## Set by `ShellRoot` rather than walked up the tree with `get_parent()`: the
## stack's parent is the `CanvasLayer`, not the root, and reaching two levels up
## by hand is how a scene reorganization becomes a null nobody expected.
var shell_root: Node

## Whether the bottom screen is allowed to pop.
##
## **True in the shell and false in an overlay host, and getting it wrong makes
## the pause menu impossible to close.** In the shell the bottom of the stack is
## the paddock and popping it would leave nothing on screen and no way back. The
## stack `circuit.gd` builds during a session starts *empty* — its depth 1 is the
## pause menu itself, and refusing to pop that is refusing to resume.
##
## Measured: with this hardcoded true, `_toggle_pause()` opened the menu and then
## could not close it. `can_pop()` already carries the per-screen half of the same
## rule — boot and the paddock both return false — so this is the per-*stack* half
## and not a second copy of it.
var keeps_bottom := true

var _stack: Array[ShellScreen] = []
var _pending_focus: ShellScreen = null
var _held := StringName()
var _repeat_clock := 0.0


func _init() -> void:
	name = "ScreenStack"
	set_anchors_preset(Control.PRESET_FULL_RECT)
	# The stack itself never eats a click; its screens do.
	mouse_filter = Control.MOUSE_FILTER_IGNORE


# --- the stack ---------------------------------------------------------------


## Add a screen on top. Synchronous through `build()`, so `depth()` and `top()`
## are correct the instant this returns; focus lands one frame later, once the
## containers have sorted and the new controls have a rect to be inside of.
func push(screen: ShellScreen) -> void:
	if not _stack.is_empty() and not screen.is_overlay():
		for below: ShellScreen in _stack:
			below.visible = false
	if not _stack.is_empty():
		_stack[-1].on_exit()

	screen.bind(shell_root, self)
	screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	_stack.append(screen)
	add_child(screen)
	screen.build()
	_pending_focus = screen
	screen_pushed.emit(screen)


## Remove the top screen and reveal the one under it. Returns the screen that was
## popped, already freed from the tree, or null at depth 1 or 0 — **the bottom of
## the stack never pops**, or the shell is left with nothing on screen and no way
## back.
func pop() -> ShellScreen:
	if _stack.is_empty():
		return null
	if keeps_bottom and _stack.size() <= 1:
		return null
	var leaving: ShellScreen = _stack.pop_back()
	leaving.on_exit()
	remove_child(leaving)
	leaving.queue_free()

	if not _stack.is_empty():
		var revealed: ShellScreen = _stack[-1]
		revealed.visible = true
		_pending_focus = revealed
	screen_popped.emit(leaving)
	return leaving


## Swap the top screen for another at the same depth.
func replace(screen: ShellScreen) -> void:
	if not _stack.is_empty():
		var leaving: ShellScreen = _stack.pop_back()
		leaving.on_exit()
		remove_child(leaving)
		leaving.queue_free()
	push(screen)


## Drop everything and start again from one screen. Used when the shell comes
## back from a session — a results screen appears at depth 1, not on top of the
## paddock the player left three screens deep.
func reset_to(screen: ShellScreen) -> void:
	while not _stack.is_empty():
		var leaving: ShellScreen = _stack.pop_back()
		leaving.on_exit()
		remove_child(leaving)
		leaving.queue_free()
	push(screen)


func top() -> ShellScreen:
	return _stack[-1] if not _stack.is_empty() else null


func depth() -> int:
	return _stack.size()


## The stack from bottom to top, for the gate. A copy: the gate walking this must
## not be able to move it.
func screens() -> Array[ShellScreen]:
	return _stack.duplicate()


# --- focus -------------------------------------------------------------------


func _process(delta: float) -> void:
	if _pending_focus != null:
		var screen := _pending_focus
		_pending_focus = null
		if is_instance_valid(screen) and screen == top():
			_land_focus(screen)
			screen.on_enter()

	if _held.is_empty():
		return
	if not Input.is_action_pressed(_held):
		_held = StringName()
		return
	_repeat_clock -= delta
	if _repeat_clock <= 0.0:
		_repeat_clock = REPEAT_RATE_S
		_navigate(DIRECTIONS[_held])


## Put focus on the screen's chosen control, and say so when there is nowhere to
## put it. A screen with focusable controls and no focus is a screen a pad cannot
## use, and it is silent — so it is reported here rather than found by hand.
func _land_focus(screen: ShellScreen) -> void:
	var target := screen.initial_focus()
	if target == null:
		if not screen.focusables().is_empty():
			push_warning("%s has %d focusable controls and initial_focus() returned null"
					% [screen.title(), screen.focusables().size()])
		return
	target.grab_focus()


## Move focus one side over, or wrap to the far end of the screen if there is
## nothing that way.
##
## `find_valid_focus_neighbor()` is the engine's own geometric walk, and it was
## measured to cross container boundaries here rather than stopping at them — the
## trap list says it does not, and on this layout it does. Either way the wrap is
## what makes a two-column form navigable, and the gate's check 5 BFS uses this
## exact call, so what the gate proves reachable is what a thumb reaches.
func _navigate(side: int) -> void:
	var screen := top()
	if screen == null:
		return
	var from := get_viewport().gui_get_focus_owner()
	if from == null or not screen.is_ancestor_of(from):
		_land_focus(screen)
		return
	var next := from.find_valid_focus_neighbor(side)
	if next != null and screen.is_ancestor_of(next):
		next.grab_focus()
		return
	var wrapped := _wrap(screen, from, side)
	if wrapped != null:
		wrapped.grab_focus()


## The furthest focusable in the opposite direction, so Down at the bottom of a
## list lands on its top rather than doing nothing. Measured by rect center, so
## it behaves the same in a column, a row and a grid.
func _wrap(screen: ShellScreen, from: Control, side: int) -> Control:
	var candidates := screen.focusables()
	if candidates.size() < 2:
		return null
	var best: Control = null
	var best_key := 0.0
	for control: Control in candidates:
		if control == from:
			continue
		var center := control.get_global_rect().get_center()
		var key := 0.0
		match side:
			SIDE_TOP: key = center.y
			SIDE_BOTTOM: key = -center.y
			SIDE_LEFT: key = center.x
			SIDE_RIGHT: key = -center.x
		if best == null or key > best_key:
			best = control
			best_key = key
	return best


# --- input -------------------------------------------------------------------


func _input(event: InputEvent) -> void:
	if _stack.is_empty():
		return

	for action: StringName in DIRECTIONS:
		if not event.is_action(action):
			continue
		# Consumed whether pressed, released or echoing, so the engine's `ui_*`
		# walk never sees any of it and there is exactly one walker.
		get_viewport().set_input_as_handled()
		if event.is_echo():
			return
		if event.is_action_pressed(action):
			_held = action
			_repeat_clock = REPEAT_DELAY_S
			_navigate(DIRECTIONS[action])
		elif _held == action:
			_held = StringName()
		return

	if event.is_action(&"menu_confirm"):
		get_viewport().set_input_as_handled()
		if event.is_action_pressed(&"menu_confirm") and not event.is_echo():
			_confirm()
		return

	if event.is_action(&"menu_back"):
		get_viewport().set_input_as_handled()
		if event.is_action_pressed(&"menu_back") and not event.is_echo():
			back()
		return


## Activate whatever holds focus. A `Button` is pressed; anything else is offered
## the action through a `menu_confirm()` method, which is how a settings row or a
## value stepper takes a press without being a button.
func _confirm() -> void:
	var focused := get_viewport().gui_get_focus_owner()
	if focused == null:
		return
	if focused.has_method("menu_confirm"):
		focused.call("menu_confirm")
		return
	if focused is BaseButton:
		var button := focused as BaseButton
		if not button.disabled:
			button.emit_signal("pressed")


## The back rule, in one place: a screen that refuses to pop keeps the stack
## where it is, and the bottom of the stack never pops. Public because pause's
## "Resume" and a form's Cancel are the same act as pressing Circle.
func back() -> bool:
	var screen := top()
	if screen == null or not screen.can_pop():
		return false
	return pop() != null
