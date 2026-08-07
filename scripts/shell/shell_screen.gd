class_name ShellScreen
extends Control

## One screen on the shell's stack. ADR-0053 §1.
##
## Subclasses override `build()` and nothing else is required. The stack calls,
## in order: `bind()`, `add_child()`, `build()`, then — one frame later, once the
## containers have sorted — `grab_focus()` on whatever `initial_focus()` returns.
##
## **The frame matters.** `grab_focus()` on a node that is not yet in the tree
## does nothing and says nothing, and a `Control` built in code is `FOCUS_NONE`
## unless it happens to be a `Button`. Both failures present identically, as "the
## pad does nothing", which is why the stack owns the ordering rather than each
## screen remembering it.
##
## Screens are built in code from a near-empty scene, like every other buildable
## scene in this repo — `valdirone.tscn`, `kartview.tscn` and `telemetry.tscn`
## each carry a header saying why. Here there is a second reason on top of the
## usual one: `assets/fonts/` is gitignored, so a `.tscn` holding an
## `ext_resource` to Liberation Sans does not load on a fresh clone.
##
## One concession against the rest of the UI: screens use real
## `Container`/`Button` trees rather than `_draw()`, which is how
## `timing_hud.gd` and `driving_hud.gd` work. **`_draw()` cannot hold focus**,
## and focus is the thing the gate checks.


## Set by the stack before `build()`. `shell` is the `ShellRoot`; go through it
## for anything that outlives a screen — the profile, the settings, the backdrop.
var shell: Node
var stack: Node


func bind(shell_root: Node, screen_stack: Node) -> void:
	shell = shell_root
	stack = screen_stack


## Construct the screen. Called once, after the node is in the tree.
func build() -> void:
	pass


## Called after `build()` and after focus has landed. Override for anything that
## needs a laid-out tree — measuring a rect, starting a timer.
func on_enter() -> void:
	pass


## Called when the screen is covered or popped. Override to stop anything that
## should not keep running underneath.
func on_exit() -> void:
	pass


## Where focus goes on entry. Returning null is legal only for a screen with no
## focusable controls at all — the gate names that screen if it has any.
func initial_focus() -> Control:
	for child: Node in _focusables(self):
		return child
	return null


## Whether `menu_back` pops this screen. **Boot and the paddock return false**:
## the paddock is the bottom of the stack and backing out of it would leave an
## empty shell, and boot is not a place you return from.
func can_pop() -> bool:
	return true


## An overlay leaves what is under it visible — the pause screen over a running
## world, a confirmation over a sheet. A normal screen hides everything below,
## so an unlit paper form does not composite over the paddock's cards.
func is_overlay() -> bool:
	return false


## For the gate's report and for the loading screen's log. Not drawn.
func title() -> String:
	return get_script().resource_path.get_file().get_basename()


## Every `FOCUS_ALL` descendant, in tree order. The gate's check 5 walks exactly
## this set, so anything focusable that navigation cannot reach is named as an
## orphan rather than discovered by a driver whose pad stops halfway down a form.
static func _focusables(root: Node) -> Array[Control]:
	var found: Array[Control] = []
	for node: Node in root.find_children("*", "Control", true, false):
		var control := node as Control
		if control.focus_mode == Control.FOCUS_ALL and control.is_visible_in_tree():
			found.append(control)
	return found


## Public spelling of the above, for the stack and the gate.
func focusables() -> Array[Control]:
	return _focusables(self)
