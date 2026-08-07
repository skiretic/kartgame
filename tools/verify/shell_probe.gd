extends SceneTree

## The shell's structural gate. ADR-0053 §3: **numbers gate structure and
## Anthony's eye gates looks**, so nothing here judges a layout — it asserts that
## every screen is reachable, that back returns where it came from, that focus
## lands somewhere a person can see, that a pad alone can reach every control,
## and that a lap time survives a relaunch.
##
##     godot --headless --path . --script tools/verify/shell_probe.gd
##       --case=<name>[,<name>]   run a subset; default is all of them
##       --break=<mode>           sabotage one property and assert the gate
##                                catches it. **Inverted exit** — see below
##       --list-breaks            print the mode names, one per line, and exit
##       --keep                   leave user://shell_probe/ behind for inspection
##
## `tools/verify/shell.sh` is the wrapper: it does the ADR-0018 double import
## first, then runs this, then runs every `--break` mode as the negative-control
## pass.
##
## ## Two rules this probe obeys because a gate that breaks the runner is worse
## ## than the bug it looks for
##
## **All profile and settings I/O goes under `user://shell_probe/`** via
## `set_base_dir()`, which is `profile_probe.gd`'s rule. A gate that wrote a
## synthetic 46.611 into Anthony's real career, or deleted it, would be a far
## worse defect than an unreachable menu.
##
## **The viewport is forced to 1600x900.** Headless, Godot reports a
## 1600x1600 root viewport — measured, not assumed — while the shipped game runs
## `canvas_items` stretch at 1600x900 with `aspect="expand"`, which pins the
## height. Check 4 measures whether a focused control's rect is *on screen*, and
## measuring that against a canvas 700 px taller than the real one would pass a
## control the player cannot see.
##
## ## Headless focus works, and that was the open question
##
## Measured before anything was built: `grab_focus()`, `has_focus()`,
## `gui_get_focus_owner()` and `find_valid_focus_neighbor()` all resolve under
## `--headless` in 4.7.1, and the neighbor walk crosses container boundaries
## rather than stopping at them. So the whole gate stays headless and never needs
## `shoot.sh`'s windowed path.
##
## Two more were measured for the navigation case and are load-bearing. A real
## `InputEventKey` duplicated out of the `InputMap` drives the whole path —
## binding, event, `ScreenStack._input`, focus — which is what makes the
## double-walk assertion below a reader check rather than a mock. And:
##
## **`Input.parse_input_event()` does not dispatch. It buffers.**
## `Input.use_accumulated_input` defaults to **true**, so an event handed to
## `parse_input_event` sits in `buffered_events` until the engine flushes it at
## the top of the *next* frame. Every assertion made immediately after a send is
## then measuring the press *before* it, and this cost an hour: the probe read
## plausible-looking focus positions that were all off by one press, `menu_back`
## appeared not to pop, and the confirm key was finally flushed one frame late —
## after the fixture had been torn down and the paddock rebuilt underneath it, so
## Enter landed on **Quit** and called `get_tree().quit()`. The whole gate exited
## 0 with no report and no crash log, which reads exactly like a hang that
## finished. `_send()` turns accumulation off and flushes by hand for this reason;
## do not remove either line.
##
## ## `--break`, and why its exit code is upside down
##
## `circuit.sh` carries a circuit that must fail to load and `input_push_probe.gd`
## has `--break`; this gate had neither, and a check that cannot fail is not a
## check. `--break=<mode>` sabotages exactly one property in-process and then
## asserts that the check aimed at it went **red**:
##
##     exit 0   the sabotage was caught — the negative control passed
##     exit 1   the sabotage went through unnoticed, or the check never ran
##
## That is the opposite of a normal run and it is deliberate: `shell.sh --break`
## is then a loop in which every mode must exit 0. `BREAK_MODES` below is the
## whole table, mode to the check it must turn red.
##
## No `--break` mode touches the repo or anything the player owns. The InputMap
## is per-process, the scene tree is thrown away, and the one thing that reaches
## disk — `--break=hex`'s saboteur `.gd` — is written under
## `user://shell_probe/break/` and scrubbed with the rest.
##
## ## The fixture screen, and why the stack needed one
##
## Boot and the paddock have **one focusable control each** in a fresh worktree,
## so check 5's BFS was `1 of 1 reached` and could not fail, `_wrap()` was never
## called, and `replace()`, `reset_to()` and the repeat clock had no caller at
## all. `FixtureScreen` below is a five-row screen this probe builds and pushes
## itself, which is what turns `ScreenStack` from unmeasured into measured. It is
## defined here rather than in `scripts/` on purpose — it is test scaffolding and
## it must never be shipped or reachable from a menu.
##
## House style: named checks, PASS or FAIL **with the measurement either way**,
## and a non-zero exit.

const SHELL_SCENE := "res://scenes/shell/shell.tscn"
const BASE_DIR := "user://shell_probe/"

## Where `--break=hex` writes its saboteur. Under `BASE_DIR`, so `_scrub()`
## already removes it.
const BREAK_DIR := BASE_DIR + "break/"

## The design canvas, from `project.godot`. See the header.
const VIEWPORT := Vector2i(1600, 900)

## Every case, in the order they run. `--case=` picks a subset.
##
## **A case name that gates a check must gate every `_record` that check makes.**
## `--case=theme` used to run six unrelated screen checks and report `4 of 10`,
## because `_walk_screens()` and `_walk_optional_screens()` recorded
## unconditionally — a subset run that quietly measures something else is worse
## than no subset at all, since the number it prints looks like an answer.
const CASES: PackedStringArray = [
	"main_scene", "reachable", "back", "focus", "pad_reach", "no_trap",
	"actions", "overlap", "no_hex", "no_sim", "one_focus", "settings_round_trip",
	"theme", "handoff", "best_lap", "results", "ghost",
	"stack", "navigate", "pause_mode",
]

## `--break=<mode>` to `[check name prefix, evidence]`. The name is matched with
## `begins_with`, because several checks are named per screen.
##
## Every mode is one sabotage. A mode that turns three checks red is fine — the
## table names the one that *must* go, and the report says how many others did.
##
## **`evidence` is why the table is pairs and not strings.** A check that was
## already failing for its own reasons turns any sabotage aimed at it into a
## green light: `--break=hex` reported `caught` while the raw-color check was red
## over `shell_backdrop.gd` and had never seen the saboteur file at all. Where the
## sabotage leaves a fingerprint in the measurement, the verdict demands it.
## An empty string means the mode has no distinguishable fingerprint and is
## trusted on the red alone — acceptable only for checks that pass in a clean
## tree, which is every one of them below.
const BREAK_MODES := {
	"focus": ["focus lands on", ""],
	"reach": ["pad reaches every control on", "orphan"],
	"occlude": ["focus is not covered on", "ColorRect"],
	"hex": ["no screen writes a raw color", "offender.gd"],
	"sim": ["no simulation state under ShellRoot", "PlantedDriver"],
	"overlap": ["menu_confirm overlaps exactly what was declared", "respawn/pad:0"],
	"settings": ["every settings row round-trips", ""],
	"nav": ["one menu_down press moves focus exactly one row", ""],
	"repeat": ["the repeat clock fires after REPEAT_DELAY_S", ""],
	"pause": ["the menu still runs under a paused tree", ""],
	"leak": ["every popped screen was freed", "leaked"],
	"back": ["the revealed screen is visible again", ""],
}

## Check 8's declared list. **Every menu binding collides with a driving one** —
## a DualSense has no spare buttons — and ADR-0053 §2 resolves that with an input
## context rather than a spare button. This table is the resolution's receipt: a
## *new* overlap fails the gate until somebody writes it down here, which is the
## difference between a decision and an accident.
##
## `pad:<index>` is a `JoyButton`; `axis:<n><sign>` a `JoyAxis` half; `key:<code>`
## a physical keycode.
const EXPECTED_OVERLAP := {
	"menu_confirm": ["shift_down/pad:0"],
	"menu_back": ["respawn/pad:1", "pause/key:4194305"],
	"menu_up": ["tune_prev/pad:11", "throttle/key:4194320"],
	"menu_down": ["tune_next/pad:12", "brake/key:4194322"],
	"menu_left": ["tune_decrease/pad:13", "steer_left/axis:0-", "steer_left/key:4194319"],
	"menu_right": ["tune_increase/pad:14", "steer_right/axis:0+", "steer_right/key:4194321"],
}

## The six menu actions, check 7's subject.
const MENU_ACTIONS: PackedStringArray = [
	"menu_confirm", "menu_back", "menu_up", "menu_down", "menu_left", "menu_right",
]

## Check 9b's forbidden list, as **engine** classes. `ClassDB.class_exists` is the
## right guard for these and only these.
const SIMULATION_CLASSES: PackedStringArray = [
	"KartBody", "PlayerDriver", "KartTuning", "KartLapTimer",
]

## Check 9b's forbidden list, as **GDScript global classes** — and the reason the
## split exists. `SessionRunner` is `scripts/game/session_runner.gd`, not a
## GDExtension class, so `ClassDB.class_exists("SessionRunner")` is *always false*
## and the old single-list check skipped it silently on every run. It is also the
## easiest one for a screen to build by accident: `SessionRunner.new()` is one
## line and needs no extension at all.
##
## `Node.find_children()` matches a script's global name as well as an engine
## class — measured in 4.7.1, one hit on a `SessionRunner` parented under a plain
## `Node` — so these are looked up the same way, just without the ClassDB guard.
const SIMULATION_SCRIPT_CLASSES: PackedStringArray = [
	"SessionRunner", "KartRig", "EngineVoiceRig", "GhostKart",
	"ChaseCamera", "CockpitCamera",
]

## And the generic catch, for a solver assembled out of stock Godot nodes rather
## than out of this project's classes. The shell has no business owning a physics
## body of any kind.
const SIMULATION_BASE_CLASSES: PackedStringArray = ["PhysicsBody3D", "VehicleWheel3D"]

## Check 9's subject: everything under `scripts/shell/`, recursively.
##
## **Not just `screens/` and `widgets/`.** Scanning those two directories missed
## `shell_backdrop.gd`, which carries `Color(0.062745, 0.094118, 0.125490)` — the
## exact three components of `ShellTheme.SCR_GROUND`, copied. That is the bug this
## check exists for: the livery round moves one constant and the backdrop behind
## every menu stays the old color, silently.
const NO_HEX_ROOT := "res://scripts/shell/"

## The one file allowed a color literal, by name. `shell_theme.gd`'s own header
## claims this exemption in its first paragraph.
const NO_HEX_EXEMPT: PackedStringArray = ["shell_theme.gd"]

## What counts as writing a color. The first two alternatives are the original
## check; the rest are the holes it had.
##
##     Color(...)  Color8(...)          the constructors
##     Color.hex/html/from_rgba8/...    the named constructors, which are how a
##                                      hex reaches a screen without a `#` in it
##     Color.WHITE                      a stock constant is still a raw color, and
##                                      it is the one a hurried screen reaches for
##     "#rrggbb"                        a hex in a string
##
## Continuation lines are joined before this runs, because `Color \` on one line
## and `(0.1, 0.2, 0.3)` on the next matched none of the above and slipped through
## a per-physical-line scan.
const HEX_PATTERN := "Color\\s*\\(|Color8\\s*\\(" \
		+ "|Color\\.(?:hex|hex64|html|from_rgba8|from_hsv|from_ok_hsl|from_string)\\s*\\(" \
		+ "|Color\\.[A-Z][A-Z0-9_]+" \
		+ "|\"#[0-9a-fA-F]{3,8}\""

## Check 11: what `circuit.gd` documents itself as taking. A menu may post a
## subset of this and nothing else — an invented argument is a menu that thinks
## it configured something.
const CIRCUIT_ARGS: PackedStringArray = [
	"track", "layout", "session", "laps", "camera", "tune", "preset", "ghost",
	"backdrop", "mesh", "auto-shift", "auto-clutch", "eye", "look",
	"throttle", "steer", "brake", "settle",
]

## Check 12's synthetic lap. A real-looking Valdirone time, and the same figure
## the mockup's results plate carries.
const SYNTHETIC_BEST := 46.611
const SYNTHETIC_TRACK := "valdirone_nuova"

## Check 14 writes into the real `user://ghosts` — see `_check_ghost()` — so it
## uses a slug that cannot collide with a circuit anybody drives.
const GHOST_TRACK := "shell_probe_only"
const GHOST_SPEED := 20.0

## Check 5's second clause: how far, in presses, the furthest control is from
## where focus starts.
##
## **Read what this does and does not catch.** BFS eccentricity over a graph of N
## nodes cannot exceed N-1, and a plain single-column list *achieves* N-1 — so
## this is a cap on how deep a menu screen may be, not a detector for a layout
## whose neighbor graph zig-zags. There is no cheap number for the second thing:
## telling a sensible column from a maze means comparing the walk order against
## the visual order, and ADR-0053 §3 gives looks to Anthony's eye. The figure is
## printed on every run either way, which is the part worth having.
##
## **Estimated**, and it says so. Nothing sources a press count; every screen in
## the mockup is under six rows and twenty is well past the point where a row is
## quicker to reach with the mouse. If a real screen ever needs more, the number
## moves and the reason goes in the commit — it must not be raised silently.
const MAX_FOCUS_HOPS := 20

## The repeat clock's injected timeline, in seconds. See `_check_navigation()`:
## these deltas are handed to `ScreenStack._process()` directly rather than
## measured off the frame clock, so the test is analytic and a slow machine
## cannot change the answer.
const REPEAT_UNDER := 0.20   # x2 = 0.40, inside REPEAT_DELAY_S 0.42
const REPEAT_TRIP := 0.05    # takes the total to 0.45, past it
const REPEAT_RATE_UNDER := 0.05
const REPEAT_RATE_TRIP := 0.06  # 0.05 + 0.06 = 0.11 = REPEAT_RATE_S

## The watchdog. Two of the phases below are state machines that advance one step
## per frame, and a runtime error inside one of them aborts the call **without**
## advancing its step — so the machine spins on the same frame forever, printing
## the same backtrace, and the gate hangs instead of failing. That happened while
## this file was being written (a `get_instance_id()` on an already-freed screen),
## and a gate CI can hang on is worse than a gate that is wrong.
##
## Six screens at three frames plus eight fixture frames plus the fixed phases is
## about 30; 600 is two orders of margin and still a couple of seconds.
const MAX_FRAMES := 600

var _args := {}
var _cases: PackedStringArray = []
var _break := ""
var _checks: Array = []
var _shell: Node
var _stack: ScreenStack
var _frames := 0
var _phase := 0
var _notes := PackedStringArray()
var _ghost_id := ""
var _hex_extra_dirs := PackedStringArray()

# The optional-screen walk's state machine. One screen per three frames, because
# the properties worth asserting — focus landed, the screen under it revealed,
# the popped screen actually freed — are all a frame late.
var _opt_keys: PackedStringArray = []
var _opt_index := 0
var _opt_step := 0
var _opt_before: ShellScreen = null
var _opt_depth := 0
var _opt_pushed_id := 0

# The fixture's state machine, and the list of things that must be dead by the
# end of it: [label, instance id].
var _fix_step := 0
var _fix_base_depth := 0
var _fix_a: ShellScreen = null
var _fix_b: ShellScreen = null
var _fix_c: ShellScreen = null
var _fix_d: ShellScreen = null
var _fix_popped_id := 0
var _fix_b_id := 0
var _expect_freed: Array = []
var _leaked: ShellScreen = null


## The probe's own five-row screen. Not a stub and not a mock — it is a real
## `ShellScreen` driven through the real `ScreenStack`, and it exists because the
## two screens that ship in a fresh worktree have one focusable control between
## them and prove nothing about navigation.
class FixtureScreen extends ShellScreen:
	const ROWS := 5

	var rows: Array[Button] = []
	var presses := 0
	var label_text := "fixture"
	var enters := 0
	var exits := 0

	func build() -> void:
		# First child, so it is *behind* everything in draw order and the occlusion
		# check has nothing to complain about. A ground added last would cover the
		# rows, which is exactly the failure `_check_occlusion()` looks for.
		add_child(ShellTheme.ground(false))
		var column := VBoxContainer.new()
		column.set_anchors_preset(Control.PRESET_FULL_RECT)
		column.add_theme_constant_override("separation", 12)
		add_child(column)
		for index: int in ROWS:
			var row := ShellTheme.row_button("row %d" % index, ShellTheme.SCR_INK)
			row.pressed.connect(_on_row_pressed)
			column.add_child(row)
			rows.append(row)

	func _on_row_pressed() -> void:
		presses += 1

	func on_enter() -> void:
		enters += 1

	func on_exit() -> void:
		exits += 1

	func initial_focus() -> Control:
		return rows[0] if not rows.is_empty() else null

	func title() -> String:
		return label_text


func _initialize() -> void:
	_args = Cmdline.parse()
	var wanted := Cmdline.as_string(_args, "case", "")
	_cases = CASES if wanted.is_empty() else wanted.split(",", false)
	# One source of truth for the mode list. `shell.sh` reads this rather than
	# carrying its own copy in bash, so a mode added here is a mode the negative
	# control pass runs without anybody remembering to add it twice.
	# Prefixed, because Godot prints its own version banner on stdout before a
	# script runs and a bare list is five engine words plus twelve mode names.
	if Cmdline.as_bool(_args, "list-breaks", false):
		for mode: String in BREAK_MODES:
			print("break-mode %s" % mode)
		quit(0)
		return

	_break = Cmdline.as_string(_args, "break", "")
	if not _break.is_empty() and not BREAK_MODES.has(_break):
		printerr("unknown --break=%s; modes are %s"
				% [_break, ", ".join(PackedStringArray(BREAK_MODES.keys()))])
		quit(2)
		return

	# **Force the shipped 16:9 canvas.** Headless, the window collapses to its
	# 64x64 minimum and `aspect="expand"` then expands the 1600-wide canvas to
	# 1600x1600 to match it — measured, and it is why a control anchored to the
	# bottom of the screen lands 640 px below the real viewport's floor and a
	# naive on-screen check passes it. `IGNORE` pins the canvas to exactly the
	# design size, which is what `expand` produces in the shipped 16:9 window.
	root.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_IGNORE
	root.content_scale_size = VIEWPORT

	_break_overlap()
	_break_hex()

	# The static checks need no scene at all, so they run first and a broken shell
	# scene still produces a useful report rather than one line about a crash.
	_check_main_scene()
	_check_actions()
	_check_overlap()
	_check_no_hex()
	_check_handoff()
	_check_settings_round_trip()
	_check_best_lap()
	_check_ghost()

	if not _needs_scene():
		_report()
		return

	var packed := load(SHELL_SCENE) as PackedScene
	if packed == null:
		_fail("shell scene loads", "%s did not load" % SHELL_SCENE)
		_report()
		return
	_shell = packed.instantiate()
	# Before `add_child`, because `_ready()` reads it. A `--script` run carries no
	# user arguments of its own, and the gate must never load a gitignored `.glb`.
	#
	# `profile-dir` is the load-bearing half. The results screen files a best lap
	# through `best_lap_store` as part of showing it, so a gate that opens it
	# against the default `user://` seats a fabricated 46.740 at the top of the
	# runner's real career, written through the same fsync'd atomic save a real
	# lap uses and indistinguishable from one. `profile_probe.gd`'s rule, extended
	# to the one probe that instantiates the whole shell rather than a lone
	# `KartProfile`.
	_shell.set("arg_override", {"backdrop": "flat", "profile-dir": BASE_DIR})
	root.add_child(_shell)


## Which cases need the shell instantiated. Kept in one place because a case added
## to `CASES` and forgotten here runs against a null `_stack` and reports a crash
## instead of a measurement.
func _needs_scene() -> bool:
	for name: String in ["reachable", "back", "focus", "pad_reach", "no_trap",
			"no_sim", "one_focus", "theme", "results", "stack", "navigate",
			"pause_mode"]:
		if _cases.has(name):
			return true
	return false


func _wants(case_name: String) -> bool:
	return _cases.has(case_name)


func _broke(mode: String) -> bool:
	return _break == mode


# --- the walk ------------------------------------------------------------------
#
# One phase per frame, because push() lands focus a frame later on purpose:
# grab_focus() on a node that is not yet laid out puts focus on a zero-sized rect,
# which is exactly what check 4 exists to catch and would otherwise cause.


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames > MAX_FRAMES:
		_fail("the probe finishes", "still in phase %d, fixture step %d, optional "
				% [_phase, _fix_step]
				+ "screen %d of %d after %d frames — a step is not advancing"
				% [_opt_index, _opt_keys.size(), _frames])
		_report()
		return true
	if _frames < 2:
		return false
	if _stack == null:
		_stack = _shell.stack() as ScreenStack
		if _stack == null:
			_fail("shell builds a stack", "ShellRoot.stack() is null")
			_report()
			return true

	match _phase:
		0:
			_break_sim()
			_break_pause()
			_check_no_sim()
			_check_theme()
			_check_pause_mode()
			_walk_screens()
		1:
			_check_focus_here("boot")
			_check_occlusion("boot")
			_check_pad_reach_here("boot")
			_check_one_focus()
			var boot := _stack.top()
			if boot != null and boot.has_method("_advance"):
				boot.call("_advance")
		2:
			_check_here("paddock")
			_break_paddock()
			_check_focus_here("paddock")
			_check_occlusion("paddock")
			_check_pad_reach_here("paddock")
			_check_no_trap_here("paddock")
			_begin_optional_walk()
		3:
			if _optional_step():
				_phase += 1
			return false
		4:
			_check_results()
		5:
			if _fixture_step():
				_phase += 1
			return false
		_:
			_report()
			return true
	_phase += 1
	return false


## Check 2, the reachable half: the stack starts at depth 1 on boot.
func _walk_screens() -> void:
	if not _wants("reachable"):
		return
	_record("stack opens at depth 1", _stack.depth() == 1,
			"depth %d, top %s" % [_stack.depth(), _top_name()])
	_check_here("boot")


# --- checks 2 and 3, over every screen the paddock can reach --------------------
#
# Push it, assert the depth and the class, pop it, assert we are back at the
# **same object** — and then the three clauses the first pass did not have:
# the revealed screen is *visible* again, focus came back *inside* it, and the
# popped screen was actually *freed* rather than left as an orphan holding a
# reference to the profile.
#
# Same object and not same class: a pop that rebuilt the paddock would look
# identical in a class check and would have thrown away every bit of state the
# screen was holding.
#
# Three frames per screen, because `queue_free()` is deferred and focus lands on
# the frame after the pop. Doing it synchronously is what let all three of those
# clauses go unasserted.


func _begin_optional_walk() -> void:
	if not (_wants("reachable") or _wants("back")):
		_opt_keys = PackedStringArray()
		return
	_opt_keys = PackedStringArray(["setup", "settings", "profile", "results",
			"loading", "pause"])


## One frame of the optional walk. Returns true when there is nothing left.
func _optional_step() -> bool:
	if _opt_index >= _opt_keys.size():
		return true
	var key := _opt_keys[_opt_index]

	match _opt_step:
		0:
			if not bool(_shell.call("has_screen", key)):
				_record("screen %s is built" % key, false, "no script at that path")
				_opt_index += 1
				return _opt_index >= _opt_keys.size()
			_opt_before = _stack.top()
			_opt_depth = _stack.depth()
			if not bool(_shell.call("open", key)):
				_record("screen %s opens" % key, false, "open() refused")
				_opt_index += 1
				return _opt_index >= _opt_keys.size()
			_record("screen %s opens" % key, _stack.depth() == _opt_depth + 1,
					"depth %d -> %d, top %s" % [_opt_depth, _stack.depth(), _top_name()])
			_opt_pushed_id = _stack.top().get_instance_id()
			_opt_step = 1
		1:
			var pushed := _stack.top()
			# **A screen with nothing focusable passes, and that is not a
			# loophole.** The loading screen deliberately has no reachable control
			# while it is loading — its "Back to setup" row sits at `FOCUS_NONE`
			# and is promoted to `FOCUS_ALL` only when `SessionRunner` refuses the
			# session. Demanding focus there would force a focus ring onto a
			# control that does nothing, which is worse than no ring at all.
			# Everything that *has* controls is still held to landing focus on one.
			var empty: bool = pushed != null and pushed.focusables().is_empty()
			_record("focus lands inside %s after the push" % key,
					pushed != null and (empty or _focus_inside(pushed)),
					"focus %s%s" % [_focus_name(),
					", and the screen has no focusable control" if empty else ""])
			if _broke("back"):
				pass
			_stack.back()
			if _broke("back") and _stack.top() != null:
				# The sabotage: the pop happened, and the screen it revealed is left
				# hidden. A player sees a black frame with a live focus ring on it.
				_stack.top().visible = false
			_opt_step = 2
		2:
			var revealed := _stack.top()
			_record("back from %s returns to the same object" % key,
					revealed == _opt_before and _stack.depth() == _opt_depth,
					"depth %d, same object %s" % [_stack.depth(), revealed == _opt_before])
			_record("the revealed screen is visible again (%s)" % key,
					revealed != null and revealed.is_visible_in_tree(),
					"visible %s" % [revealed != null and revealed.is_visible_in_tree()])
			_record("focus returns inside the revealed screen (%s)" % key,
					revealed != null and _focus_inside(revealed),
					"focus %s" % _focus_name())
			_record("every popped screen was freed (%s)" % key,
					not is_instance_id_valid(_opt_pushed_id),
					"instance %d %s" % [_opt_pushed_id,
					"alive" if is_instance_id_valid(_opt_pushed_id) else "freed"])
			_opt_step = 0
			_opt_index += 1
	return _opt_index >= _opt_keys.size()


func _check_here(expected: String) -> void:
	if not _wants("reachable"):
		return
	var top := _stack.top()
	_record("top screen is %s" % expected, top != null and top.title() == expected,
			"top is %s" % _top_name())


## Check 4. Focus is non-null, a descendant of the top screen, visible in tree,
## **and its global rect intersects the viewport** — the clause a naive check
## misses, and the one that catches a control focused before its container sorted.
func _check_focus_here(screen: String) -> void:
	if not _wants("focus"):
		return
	var top := _stack.top()
	if top == null:
		_record("focus lands on %s" % screen, false, "no screen on the stack")
		return
	var focused := root.gui_get_focus_owner()
	if focused == null:
		_record("focus lands on %s" % screen, top.focusables().is_empty(),
				"nothing focused, %d focusable" % top.focusables().size())
		return
	var rect := focused.get_global_rect()
	var on_screen := rect.intersects(Rect2(Vector2.ZERO, Vector2(VIEWPORT)))
	var ok := top.is_ancestor_of(focused) and focused.is_visible_in_tree() \
			and on_screen and rect.get_area() > 0.0
	_record("focus lands on %s" % screen, ok,
			"%s at %s, on screen %s" % [focused.name, rect, on_screen])


## Check 4b. The focused control is not **behind** something. A rect inside the
## viewport that an opaque sibling drawn later covers completely is, from the
## player's side, the same failure as a rect off the edge of the screen: a focus
## ring nobody can see and a menu that appears to do nothing.
##
## Draw order is depth-first child order, so an occluder is a control that comes
## *after* the focused one in that order, is not one of its ancestors, is opaque,
## and **encloses** its rect. Enclosure and not intersection, deliberately: a
## partial overlap is a design question for Anthony's eye and this file does not
## have one.
func _check_occlusion(screen: String) -> void:
	if not _wants("focus"):
		return
	var top := _stack.top()
	if top == null:
		return
	var focused := root.gui_get_focus_owner()
	if focused == null or not top.is_ancestor_of(focused):
		return
	var order := _draw_order(top)
	var at := order.find(focused)
	if at < 0:
		_record("focus is not covered on %s" % screen, false,
				"%s is not in %s's draw order" % [focused.name, screen])
		return
	var target := focused.get_global_rect()
	var covered := PackedStringArray()
	for index: int in range(at + 1, order.size()):
		var other: Control = order[index]
		if focused.is_ancestor_of(other) or not other.is_visible_in_tree():
			continue
		if not _is_opaque(other):
			continue
		if other.get_global_rect().encloses(target):
			covered.append("%s(%s)" % [other.name, other.get_class()])
	_record("focus is not covered on %s" % screen, covered.is_empty(),
			"%s at %s, %d control%s drawn over it%s" % [focused.name, target,
			covered.size(), "" if covered.size() == 1 else "s",
			"" if covered.is_empty() else ": " + ", ".join(covered)])


## Depth-first child order under a node, which is also `CanvasItem` draw order.
static func _draw_order(from: Node) -> Array[Control]:
	var found: Array[Control] = []
	for child: Node in from.get_children():
		var control := child as Control
		if control != null:
			found.append(control)
		found.append_array(_draw_order(child))
	return found


## Opaque enough to hide what is under it. Conservative on purpose — anything this
## cannot prove opaque is treated as transparent, so the check never invents an
## occluder out of a panel with a translucent wash.
static func _is_opaque(control: Control) -> bool:
	if control.modulate.a < 0.999 or control.self_modulate.a < 0.999:
		return false
	var rect := control as ColorRect
	if rect != null:
		return rect.color.a >= 0.999
	if control is Panel or control is PanelContainer:
		var box := control.get_theme_stylebox("panel") as StyleBoxFlat
		return box != null and box.bg_color.a >= 0.999
	return false


## Check 5. BFS over `find_valid_focus_neighbor()` on four sides from the entry
## control must reach every `FOCUS_ALL` descendant. **Names the orphan**, because
## "some control is unreachable" is not a sentence anybody can act on.
##
## The second clause is the eccentricity: the furthest control, in presses, from
## where focus starts. Reachable-in-thirty is reachable and is still a maze, and
## the BFS on its own cannot tell the two apart.
func _check_pad_reach_here(screen: String) -> void:
	if not _wants("pad_reach"):
		return
	var top := _stack.top()
	if top == null:
		return
	var all := top.focusables()
	if all.is_empty():
		_record("pad reaches every control on %s" % screen, true, "no focusable controls")
		return
	var start := top.initial_focus()
	if start == null:
		_record("pad reaches every control on %s" % screen, false,
				"%d focusable, initial_focus() is null" % all.size())
		return

	var seen := {start: 0}
	var queue: Array[Control] = [start]
	var eccentricity := 0
	while not queue.is_empty():
		var from: Control = queue.pop_front()
		var hops: int = seen[from] + 1
		for side: int in [SIDE_TOP, SIDE_BOTTOM, SIDE_LEFT, SIDE_RIGHT]:
			var next := from.find_valid_focus_neighbor(side)
			if next != null and not seen.has(next) and top.is_ancestor_of(next):
				seen[next] = hops
				eccentricity = maxi(eccentricity, hops)
				queue.append(next)

	var orphans := PackedStringArray()
	for control: Control in all:
		if not seen.has(control):
			orphans.append("%s(%s)" % [control.name, control.get_class()])
	_record("pad reaches every control on %s" % screen, orphans.is_empty(),
			"%d of %d reached%s" % [seen.size(), all.size(),
			"" if orphans.is_empty() else ", orphans: " + ", ".join(orphans)])
	_record("%s is no deeper than %d presses" % [screen, MAX_FOCUS_HOPS],
			eccentricity <= MAX_FOCUS_HOPS,
			"furthest of %d control%s is %d press%s from the entry control"
			% [all.size(), "" if all.size() == 1 else "s",
			eccentricity, "" if eccentricity == 1 else "es"])


## Check 6. `menu_back` pops, from every focusable on the screen.
##
## Exempt where `can_pop()` is false — the paddock is the bottom of the stack and
## boot is not somewhere you return from, so refusing is the correct behavior and
## the check asserts the refusal instead.
func _check_no_trap_here(screen: String) -> void:
	if not _wants("no_trap"):
		return
	var top := _stack.top()
	if top == null:
		return
	if not top.can_pop():
		var depth := _stack.depth()
		var popped := _stack.back()
		_record("%s refuses to pop, and stays put" % screen,
				not popped and _stack.depth() == depth,
				"popped %s, depth %d" % [popped, _stack.depth()])
		return
	var before := _stack.depth()
	for control: Control in top.focusables():
		control.grab_focus()
		if _stack.back():
			_record("menu_back pops %s from any control" % screen, true,
					"depth %d -> %d" % [before, _stack.depth()])
			return
	_record("menu_back pops %s from any control" % screen, false,
			"depth stayed at %d" % _stack.depth())


## Check 9c. Exactly one `Control` in the whole tree reports `has_focus()`.
func _check_one_focus() -> void:
	if not _wants("one_focus"):
		return
	var holders := PackedStringArray()
	for node: Node in root.find_children("*", "Control", true, false):
		if (node as Control).has_focus():
			holders.append(node.name)
	_record("exactly one control has focus", holders.size() == 1,
			"%d: %s" % [holders.size(), ", ".join(holders)])


## Check 9b. No simulation state anywhere under `ShellRoot`.
##
## **And a count of what was actually looked for**, because the old version
## guarded every class behind `ClassDB.class_exists()` and passed with a clean
## line when the extension was not built at all — a green check measuring nothing,
## which is the exact shape this repo keeps getting bitten by. If nothing is
## checkable, that is a failure and it says so.
func _check_no_sim() -> void:
	if not _wants("no_sim"):
		return
	var found := PackedStringArray()
	var looked := 0
	for engine_class: String in SIMULATION_CLASSES:
		if not ClassDB.class_exists(engine_class):
			continue
		looked += 1
		for node: Node in _shell.find_children("*", engine_class, true, false):
			found.append("%s(%s)" % [node.name, engine_class])
	for script_class: String in SIMULATION_SCRIPT_CLASSES:
		# No ClassDB guard: these are GDScript globals and ClassDB has never heard
		# of them. `find_children` resolves a script's global name directly.
		looked += 1
		for node: Node in _shell.find_children("*", script_class, true, false):
			found.append("%s(%s)" % [node.name, script_class])
	for base_class: String in SIMULATION_BASE_CLASSES:
		looked += 1
		for node: Node in _shell.find_children("*", base_class, true, false):
			found.append("%s(%s)" % [node.name, base_class])
	_record("no simulation state under ShellRoot", found.is_empty() and looked > 0,
			"%d class%s searched, %d found%s" % [looked, "" if looked == 1 else "es",
			found.size(), "" if found.is_empty() else ": " + ", ".join(found)])
	if looked < SIMULATION_CLASSES.size() + SIMULATION_SCRIPT_CLASSES.size() \
			+ SIMULATION_BASE_CLASSES.size():
		_notes.append("check 9b: %d of %d classes were resolvable — the extension "
				% [looked, SIMULATION_CLASSES.size() + SIMULATION_SCRIPT_CLASSES.size()
				+ SIMULATION_BASE_CLASSES.size()]
				+ "is not built, so the engine-class half proved less than it reads")


## Check 15. **A menu still runs when the tree is paused.**
##
## `process_mode` is inherited, the shell never pauses the tree itself, and the UI
## layer is `PROCESS_MODE_ALWAYS` for exactly one reason: a pause overlay whose
## `_process` stops the moment something else pauses is a menu you cannot leave —
## no repeat clock, no `_pending_focus` landing, and Circle read but never acted
## on. `shell_root.gd` says so in its header and nothing asserted it.
##
## The whole chain is measured, not just the layer, because `process_mode` is
## inherited and a screen or a widget setting its own `PROCESS_MODE_INHERIT` puts
## the freeze back one level down.
func _check_pause_mode() -> void:
	if not _wants("pause_mode"):
		return
	var layer := _shell.get_node_or_null("UI")
	if layer == null:
		_record("the menu still runs under a paused tree", false,
				"ShellRoot has no UI layer")
		return
	_record("the UI layer is PROCESS_MODE_ALWAYS",
			layer.process_mode == Node.PROCESS_MODE_ALWAYS,
			"process_mode %d" % layer.process_mode)

	var was_paused := paused
	paused = true
	var frozen := PackedStringArray()
	for node: Node in [layer, _stack]:
		if not node.can_process():
			frozen.append("%s(%s)" % [node.name, node.get_class()])
	var top := _stack.top()
	if top != null:
		if not top.can_process():
			frozen.append("%s(top screen)" % top.title())
		for control: Control in top.focusables():
			if not control.can_process():
				frozen.append(control.name)
	paused = was_paused
	_record("the menu still runs under a paused tree", frozen.is_empty(),
			"tree paused, %d node%s stopped processing%s" % [frozen.size(),
			"" if frozen.size() == 1 else "s",
			"" if frozen.is_empty() else ": " + ", ".join(frozen)])


## Check 10. The theme resolves, and **says which font path it used**. A fallback
## is not a failure on a fresh clone — the TTFs are gitignored and reproduced by
## a fetch script — but a still shot on the wrong face has to be diagnosable from
## the log rather than by eye.
##
## `face != null and width > 0` was the old assertion and it **could not fail**:
## `face()` returns a freshly constructed `FontVariation` on every path including
## the fallback, and any font at all measures a string wider than zero. So the
## line was green whatever happened. What replaces it are four things that can go
## red — the cache key, the tracking arithmetic the header states in words, the
## scale floor, and tabular figures where the real face resolved.
func _check_theme() -> void:
	if not _wants("theme"):
		return
	var source := ShellTheme.font_source()
	var face := ShellTheme.face(ShellTheme.Weight.BOLD, 4)
	var width := face.get_string_size("0:46.611", HORIZONTAL_ALIGNMENT_LEFT, -1.0,
			int(ShellTheme.T_HERO)).x
	_notes.append("font source: %s" % source)
	_notes.append("hero figure 0:46.611 at %d px measures %.1f px"
			% [int(ShellTheme.T_HERO), width])

	# The cache is keyed on weight *and* tracking. A key that dropped either would
	# hand a bold label the regular face, silently, on every call after the first.
	_record("the face cache is keyed on weight and tracking",
			ShellTheme.face(ShellTheme.Weight.BOLD, 4) == face
			and ShellTheme.face(ShellTheme.Weight.REGULAR, 4) != face
			and ShellTheme.face(ShellTheme.Weight.BOLD, 0) != face,
			"bold/4 is cached, and differs from regular/4 and bold/0")

	# `shell_theme.gd`'s own header: "at the sizes above that is +1 and +12
	# respectively". An assertion, not a comment — move a size or an em and this
	# is what says the tracking moved with it.
	var label_track := ShellTheme.tracking(ShellTheme.T_COLHEAD, ShellTheme.TRACK_LABEL_EM)
	var mark_track := ShellTheme.tracking(ShellTheme.T_WORDMARK, ShellTheme.TRACK_WORDMARK_EM)
	_record("tracking is +1 on a kicker and +12 on the wordmark",
			label_track == 1 and mark_track == 12,
			"%d px at %.0f/%0.2fem, %d px at %.0f/%0.2fem" % [
			label_track, ShellTheme.T_COLHEAD, ShellTheme.TRACK_LABEL_EM,
			mark_track, ShellTheme.T_WORDMARK, ShellTheme.TRACK_WORDMARK_EM])

	var scale := ShellTheme.scale_for(Vector2(VIEWPORT))
	_record("type scale is 1.0 at the design height", absf(scale - 1.0) < 1e-6,
			"%.4f at %d px" % [scale, VIEWPORT.y])
	var floored := ShellTheme.scale_for(Vector2(320.0, 180.0))
	_record("type scale floors at 0.5", absf(floored - 0.5) < 1e-6,
			"%.4f at 180 px, which is 0.2 unfloored" % floored)

	# FRONTEND §3 calls tabular figures non-negotiable on anything that reports a
	# time, and the property that phrase means is **two lap times of the same
	# shape measure the same width**. Measured that way rather than by comparing
	# single-glyph advances, because the advances are not where this breaks:
	# Liberation Sans gives every digit 17.000 px at 30 px, with or without `tnum`,
	# and `1:11.111` still comes out 5.00 px narrower than `0:00.000`. The
	# difference is **kerning between digit pairs**, which `tnum` does not touch —
	# Liberation Sans has no `tnum` table at all, so the feature `shell_theme.gd`
	# sets is a no-op and the wobble it was meant to prevent is still there.
	# `opentype_features` with `kern: 0` flattens all four to 118.00 px, measured.
	#
	# Asserted only against the real face: a fresh clone on the engine fallback is
	# a declared degraded mode, and failing it here would put a red line on a
	# checkout that has done nothing wrong.
	var samples := PackedStringArray(["0:00.000", "1:11.111", "1:47.999", "0:46.611"])
	var narrowest := 1.0e30
	var widest := 0.0
	var measured := PackedStringArray()
	for sample: String in samples:
		var span := face.get_string_size(sample, HORIZONTAL_ALIGNMENT_LEFT, -1.0,
				int(ShellTheme.T_HERO)).x
		narrowest = minf(narrowest, span)
		widest = maxf(widest, span)
		measured.append("%s=%.2f" % [sample, span])
	var spread := widest - narrowest
	if source == ShellTheme.FONT_DIR:
		_record("lap times of the same shape measure the same width", spread < 0.5,
				"%s — spread %.2f px at %d px, which is %.1f%% of the column"
				% [", ".join(measured), spread, int(ShellTheme.T_HERO),
				100.0 * spread / maxf(widest, 1.0)])
	else:
		_notes.append("tabular figures unasserted: the face is the engine fallback, "
				+ "measured %s" % ", ".join(measured))


# --- the fixture: what ScreenStack does, measured ------------------------------
#
# `push`, `replace`, `reset_to`, the reveal, the free, focus, `_wrap()`, the
# single-walk and the repeat clock. None of it had a caller before this.


func _fixture_step() -> bool:
	if not (_wants("stack") or _wants("navigate")):
		return true

	match _fix_step:
		0:
			_fix_base_depth = _stack.depth()
			_fix_a = _new_fixture("fixture-a")
			_stack.push(_fix_a)
			_fix_step = 1
		1:
			_check_fixture_push()
			_fix_b = _new_fixture("fixture-b")
			_expect_freed.append(["fixture-a", _fix_a.get_instance_id()])
			_stack.replace(_fix_b)
			if _wants("stack"):
				_record("replace() keeps the depth",
						_stack.depth() == _fix_base_depth + 1 and _stack.top() == _fix_b,
						"depth %d, top %s" % [_stack.depth(), _top_name()])
			_fix_step = 2
		2:
			if _wants("stack"):
				_record("replace() frees the screen it swapped out",
						not is_instance_id_valid(_expect_freed[0][1]),
						"fixture-a instance %d %s" % [_expect_freed[0][1],
						"alive" if is_instance_id_valid(_expect_freed[0][1]) else "freed"])
				_record("focus lands inside the replacement", _focus_inside(_fix_b),
						"focus %s" % _focus_name())
			_fix_c = _new_fixture("fixture-c")
			_stack.push(_fix_c)
			_fix_step = 3
		3:
			_fix_popped_id = _fix_c.get_instance_id()
			if _wants("stack"):
				_record("push() adds a screen and hides the one below",
						_stack.depth() == _fix_base_depth + 2 and _stack.top() == _fix_c
						and not _fix_b.visible,
						"depth %d, top %s, the screen below is visible %s"
						% [_stack.depth(), _top_name(), _fix_b.visible])
			_stack.back()
			if _broke("back"):
				# The sabotage: the pop happened and the screen it revealed is left
				# hidden. A player gets a black frame with a live focus ring on it.
				_fix_b.visible = false
			if _broke("leak"):
				# Not a real leak — `queue_free()` cannot be cancelled — but a live
				# `ShellScreen` the freed-list is told to expect dead. It exercises the
				# assertion, and nothing more than that is claimed for it.
				_leaked = _new_fixture("leaked")
				_expect_freed.append(["leaked", _leaked.get_instance_id()])
			_expect_freed.append(["fixture-c", _fix_popped_id])
			_fix_step = 4
		4:
			if _wants("stack"):
				_record("back() reveals the same object it covered",
						_stack.top() == _fix_b and _stack.depth() == _fix_base_depth + 1,
						"depth %d, same object %s" % [_stack.depth(), _stack.top() == _fix_b])
				_record("the revealed screen is visible again (fixture)",
						_fix_b.visible and _fix_b.is_visible_in_tree(),
						"visible %s, visible in tree %s"
						% [_fix_b.visible, _fix_b.is_visible_in_tree()])
				_record("focus returns inside the revealed screen (fixture)",
						_focus_inside(_fix_b), "focus %s" % _focus_name())
				_record("on_exit fired for the screen that was covered",
						_fix_b.exits >= 1 and _fix_b.enters >= 2,
						"fixture-b: %d enters, %d exits" % [_fix_b.enters, _fix_b.exits])
				var alive := PackedStringArray()
				for entry: Array in _expect_freed:
					if is_instance_id_valid(entry[1]):
						alive.append("%s(%d)" % [entry[0], entry[1]])
				_record("every popped screen was freed (fixture)", alive.is_empty(),
						"%d expected dead%s" % [_expect_freed.size(),
						"" if alive.is_empty() else ", still alive: " + ", ".join(alive)])
			_fix_d = _new_fixture("fixture-d")
			# The id is taken **before** the call that frees it. Reading it
			# afterwards is `Cannot call method 'get_instance_id' on a previously
			# freed instance`, which aborts this function, leaves `_fix_step` where
			# it was and spins the state machine forever.
			_fix_b_id = _fix_b.get_instance_id()
			_stack.reset_to(_fix_d)
			_fix_step = 5
		5:
			if _wants("stack"):
				_record("reset_to() collapses the stack to one screen",
						_stack.depth() == 1 and _stack.top() == _fix_d,
						"depth %d, top %s" % [_stack.depth(), _top_name()])
				_record("reset_to() frees everything it discarded",
						not is_instance_id_valid(_fix_b_id),
						"fixture-b instance %d %s" % [_fix_b_id,
						"alive" if is_instance_id_valid(_fix_b_id) else "freed"])
			_fix_step = 6
		6:
			_check_navigation(_fix_d)
			# Put the shell back where it was found. `reset_to` above threw the
			# paddock away, and a gate that hands the report a shell in a state no
			# player can reach is a gate nobody can debug from.
			_shell.call("reset_to", "paddock")
			_fix_step = 7
		7:
			if _wants("stack"):
				_record("the shell returns to the paddock after the fixture",
						_stack.depth() == 1 and _top_name() == "paddock",
						"depth %d, top %s" % [_stack.depth(), _top_name()])
			return true
	return false


func _new_fixture(label: String) -> ShellScreen:
	var screen := FixtureScreen.new()
	screen.label_text = label
	return screen


func _check_fixture_push() -> void:
	if not _wants("stack"):
		return
	_record("push() lands on the fixture at depth+1",
			_stack.depth() == _fix_base_depth + 1 and _stack.top() == _fix_a,
			"depth %d -> %d, top %s" % [_fix_base_depth, _stack.depth(), _top_name()])
	_record("focus lands on the fixture's first row",
			root.gui_get_focus_owner() == _fix_a.rows[0],
			"focus %s, expected %s" % [_focus_name(), _fix_a.rows[0].name])

	# The five rows have to have sorted into five distinct rects, or every
	# navigation assertion below is measuring a pile of zero-sized controls in the
	# same place and would agree with itself whatever the stack did.
	var centers := PackedFloat32Array()
	var degenerate := 0
	for row: Button in _fix_a.rows:
		var rect := row.get_global_rect()
		if rect.get_area() <= 0.0:
			degenerate += 1
		centers.append(rect.get_center().y)
	var ordered := true
	for index: int in range(1, centers.size()):
		if centers[index] <= centers[index - 1]:
			ordered = false
	_record("the fixture's rows sorted into a column",
			degenerate == 0 and ordered and centers.size() == FixtureScreen.ROWS,
			"%d rows, %d degenerate, centers %s" % [centers.size(), degenerate,
			", ".join(_floats(centers))])


## Checks 16 to 20. The whole menu-input path, on a screen with five rows.
##
## Everything here goes through a **real `InputEventKey` duplicated out of the
## `InputMap`**, dispatched with `Input.parse_input_event()`. That is what makes
## this a reader check in #169's sense: erase the binding and it goes red, which
## is exactly what `--break=nav` does.
##
## The single-walk assertion is the one this file most needed. `screen_stack.gd`'s
## header is three paragraphs arguing that `menu_down` and `ui_down` are bound to
## the *same* key, that the engine's GUI pass walks focus on `ui_down` after
## `_input()`, and that reading directions in `_unhandled_input` would therefore
## move the selection **twice per press**. Nothing asserted it. Down from row 0
## landing on row 2 is that bug, and it is invisible in the code that causes it.
##
## The repeat clock is driven by handing `ScreenStack._process()` chosen deltas
## rather than by waiting: `MainLoop::_process` runs before node processing in the
## same frame, so no engine tick lands between these calls and the timeline is
## exact. Waiting on the frame clock instead would make the answer depend on how
## busy the machine is.
func _check_navigation(screen: ShellScreen) -> void:
	if not _wants("navigate"):
		return
	var fixture := screen as FixtureScreen
	if fixture == null or fixture.rows.size() < 3:
		_record("one menu_down press moves focus exactly one row", false,
				"the fixture screen is not on top")
		return
	var rows := fixture.rows

	if _broke("nav"):
		# The sabotage: take the keyboard binding away. Every assertion below then
		# measures a press that reaches nothing, which is the failure #169 exists
		# for and the reason this test uses the real InputMap rather than a
		# hand-built action event.
		InputMap.action_erase_events(&"menu_down")

	rows[0].grab_focus()
	if not _send(&"menu_down", true):
		_record("one menu_down press moves focus exactly one row", false,
				"menu_down has no InputEventKey to press")
		return
	_record("one menu_down press moves focus exactly one row",
			root.gui_get_focus_owner() == rows[1],
			"row 0 -> %s; row 2 would be the engine's ui_down walking as well"
			% _focus_name())

	if _broke("repeat"):
		# The sabotage: the thumb comes off the key before the delay elapses.
		# `ScreenStack._process` re-checks `Input.is_action_pressed(_held)` on every
		# tick, so the clock must never fire — and if it fires anyway, the repeat is
		# running off a latched flag rather than off the held control.
		_send(&"menu_down", false)
		Input.action_release(&"menu_down")

	# Held. `_held` is set, `_repeat_clock` is REPEAT_DELAY_S, and nothing else
	# touches the stack until the release below.
	_stack._process(REPEAT_UNDER)
	_stack._process(REPEAT_UNDER)
	_record("the repeat clock waits out REPEAT_DELAY_S",
			root.gui_get_focus_owner() == rows[1],
			"%.2f s held, focus still %s, delay is %.2f s"
			% [REPEAT_UNDER * 2.0, _focus_name(), ScreenStack.REPEAT_DELAY_S])
	_stack._process(REPEAT_TRIP)
	_record("the repeat clock fires after REPEAT_DELAY_S",
			root.gui_get_focus_owner() == rows[2],
			"%.2f s held, focus %s, expected %s"
			% [REPEAT_UNDER * 2.0 + REPEAT_TRIP, _focus_name(), rows[2].name])
	_stack._process(REPEAT_RATE_UNDER)
	var early := root.gui_get_focus_owner() == rows[2]
	_stack._process(REPEAT_RATE_TRIP)
	_record("the repeat rate is REPEAT_RATE_S",
			early and root.gui_get_focus_owner() == rows[3],
			"no move at %.2f s into the repeat, one move at %.2f s, rate is %.2f s"
			% [REPEAT_RATE_UNDER, REPEAT_RATE_UNDER + REPEAT_RATE_TRIP,
			ScreenStack.REPEAT_RATE_S])
	_send(&"menu_down", false)
	Input.action_release(&"menu_down")

	# Reversible. The BFS proves every control is reachable; it does not prove that
	# going back the way you came returns you. A layout can satisfy one and not the
	# other, and the one a person experiences is this one.
	var irreversible := PackedStringArray()
	for index: int in range(0, rows.size() - 1):
		rows[index].grab_focus()
		_tap(&"menu_down")
		var down := root.gui_get_focus_owner()
		_tap(&"menu_up")
		if root.gui_get_focus_owner() != rows[index]:
			irreversible.append("row %d down to %s, up to %s"
					% [index, down.name if down != null else "<none>", _focus_name()])
	_record("down then up returns to the row it started on", irreversible.is_empty(),
			"%d row%s tested%s" % [rows.size() - 1, "" if rows.size() == 2 else "s",
			"" if irreversible.is_empty() else ", " + "; ".join(irreversible)])

	# `_wrap()`, which nothing else calls. Down at the bottom of a list has to
	# reach the top or the last row is a dead end with a live focus ring on it.
	rows[-1].grab_focus()
	_tap(&"menu_down")
	var wrapped_down := root.gui_get_focus_owner() == rows[0]
	rows[0].grab_focus()
	_tap(&"menu_up")
	var wrapped_up := root.gui_get_focus_owner() == rows[-1]
	_record("navigation wraps at both ends of a list", wrapped_down and wrapped_up,
			"down at the bottom wrapped %s, up at the top wrapped %s"
			% [wrapped_down, wrapped_up])

	# `_confirm()`. `ui_accept` is bound to Enter as well, so a confirm that walked
	# through the GUI pass too would press the row twice.
	var before := fixture.presses
	rows[2].grab_focus()
	_tap(&"menu_confirm")
	_record("menu_confirm presses the focused row exactly once",
			fixture.presses == before + 1,
			"%d press%s recorded" % [fixture.presses - before,
			"" if fixture.presses - before == 1 else "es"])

	# And the back key, through the real event rather than through `stack.back()`.
	# `_check_no_trap_here` proves the paddock refuses; nothing proved a screen that
	# accepts actually pops when the key is pressed.
	var depth := _stack.depth()
	_stack.push(_new_fixture("fixture-back"))
	var pushed := _stack.depth()
	_tap(&"menu_back")
	_record("menu_back pops through the real binding",
			pushed == depth + 1 and _stack.depth() == depth,
			"depth %d -> %d -> %d" % [depth, pushed, _stack.depth()])


## Press and release one menu action, through its real keyboard binding.
func _tap(action: StringName) -> void:
	_send(action, true)
	_send(action, false)
	Input.action_release(action)


## Dispatch the action's own `InputEventKey`, duplicated out of the `InputMap`, so
## the whole binding-to-focus path is under test. Returns false when the action
## has no key event at all, which is itself a finding.
##
## The two `Input` calls around the dispatch are not belt and braces — see the
## header. Accumulation left on turns every assertion in this file into a reading
## of the previous press, and the last press of the run lands on whatever screen
## exists a frame later.
func _send(action: StringName, pressed: bool) -> bool:
	for event: InputEvent in InputMap.action_get_events(action):
		var key := event as InputEventKey
		if key == null:
			continue
		var copy := key.duplicate() as InputEventKey
		copy.pressed = pressed
		copy.echo = false
		Input.use_accumulated_input = false
		Input.parse_input_event(copy)
		Input.flush_buffered_events()
		return true
	return false


# --- results --------------------------------------------------------------------


## Check 13. The results screen renders the ledger it is handed: 12 synthetic
## rows, 2 struck, and the pinned best equals `min` over the **valid** rows only.
func _check_results() -> void:
	if not _wants("results"):
		return
	if not bool(_shell.call("has_screen", "results")):
		_record("results renders a synthetic ledger", false, "results_screen.gd is not built")
		return
	var rows := _synthetic_ledger()
	var best := 1.0e30
	for row: Dictionary in rows:
		if bool(row["valid"]):
			best = minf(best, float(row["time_s"]))
	SessionRequest.deliver({
		"track_name": "Valdirone Nuova", "layout_name": "forward",
		"laps": rows, "has_best": true, "best_lap_s": best,
		"laps_completed": rows.size(), "valid_laps": rows.size() - 2,
		"invalid_laps": 2, "outcome": "probe", "elapsed_s": 600.0,
	})
	_shell.call("open", "results")
	var top := _stack.top()
	var shown: float = top.call("pinned_best_s") if top.has_method("pinned_best_s") else -1.0
	_record("results pins min over valid laps", absf(shown - best) < 1e-6,
			"showed %.3f, min of valid is %.3f" % [shown, best])
	var drawn: int = top.call("row_count") if top.has_method("row_count") else -1
	_record("results renders every lap it was given", drawn == rows.size(),
			"%d rows drawn of %d" % [drawn, rows.size()])


## Twelve laps, two struck, one obviously fastest and one **faster still but
## struck** — the row that catches a results screen sorting on time and ignoring
## validity, which would put an off-track lap at the top of a classification.
func _synthetic_ledger() -> Array:
	var rows: Array = []
	for lap: int in 12:
		var struck := lap == 4 or lap == 9
		rows.append({
			"lap": lap + 1,
			"time_s": (45.90 if struck else 47.40 - 0.06 * lap),
			"sectors": PackedFloat64Array([15.1, 16.2, 16.0]),
			"speed_kmh": 128.0 + lap * 0.3,
			"valid": not struck,
			"reason": "off track" if struck else "",
		})
	return rows


# --- static checks --------------------------------------------------------------


## Check 1. The demo definition's first line, as one string comparison.
func _check_main_scene() -> void:
	if not _wants("main_scene"):
		return
	var main := String(ProjectSettings.get_setting("application/run/main_scene", ""))
	_record("run/main_scene is the shell", main == SHELL_SCENE, main)


## Check 7. #169's reader check, applied to menus: the six actions exist, and each
## has at least one joypad event — a menu bound to the keyboard alone is a menu a
## driver holding a pad cannot use, which is exactly the failure that milestone
## shipped.
func _check_actions() -> void:
	if not _wants("actions"):
		return
	for action: String in MENU_ACTIONS:
		if not InputMap.has_action(action):
			_record("action %s exists" % action, false, "absent from the InputMap")
			continue
		var pads := 0
		var keys := 0
		for event: InputEvent in InputMap.action_get_events(action):
			if event is InputEventJoypadButton or event is InputEventJoypadMotion:
				pads += 1
			elif event is InputEventKey:
				keys += 1
		# The key half is new, and it is not cosmetic: `_check_navigation()` drives
		# the whole menu path through a duplicated `InputEventKey`, so an action
		# with no key binding silently un-tests itself.
		_record("action %s has a pad and a key binding" % action, pads >= 1 and keys >= 1,
				"%d joypad event%s, %d key event%s" % [pads, "" if pads == 1 else "s",
				keys, "" if keys == 1 else "s"])


## Check 8. The overlap between menu and driving bindings equals the declared set.
func _check_overlap() -> void:
	if not _wants("overlap"):
		return
	var driving := {}
	for action: String in InputMap.get_actions():
		var text := String(action)
		if text.begins_with("ui_") or MENU_ACTIONS.has(text):
			continue
		for event: InputEvent in InputMap.action_get_events(action):
			var key := _event_key(event)
			if not key.is_empty():
				driving[key] = text

	for action: String in MENU_ACTIONS:
		if not InputMap.has_action(action):
			continue
		var found := PackedStringArray()
		for event: InputEvent in InputMap.action_get_events(action):
			var key := _event_key(event)
			if driving.has(key):
				found.append("%s/%s" % [driving[key], key])
		found.sort()
		var expected := PackedStringArray(EXPECTED_OVERLAP.get(action, []))
		expected.sort()
		_record("%s overlaps exactly what was declared" % action,
				found == expected,
				"found [%s] expected [%s]" % [", ".join(found), ", ".join(expected)])


static func _event_key(event: InputEvent) -> String:
	if event is InputEventKey:
		var key := event as InputEventKey
		return "key:%d" % (key.physical_keycode if key.physical_keycode != 0 else key.keycode)
	if event is InputEventJoypadButton:
		return "pad:%d" % (event as InputEventJoypadButton).button_index
	if event is InputEventJoypadMotion:
		var motion := event as InputEventJoypadMotion
		return "axis:%d%s" % [motion.axis, "+" if motion.axis_value > 0.0 else "-"]
	return ""


## Check 9. No screen or widget writes a raw color. `shell_theme.gd` is the only
## file permitted a hex, and this is the scan that keeps it that way — because a
## theme with one hardcoded blue in it is a theme the livery round cannot move.
##
## Recursive, over all of `scripts/shell/`, and reading **logical** lines rather
## than physical ones. Both were holes: the old scan looked in two directories
## that do not contain `shell_backdrop.gd`, and a `Color \` split across a
## continuation matched nothing on either half.
func _check_no_hex() -> void:
	if not _wants("no_hex"):
		return
	var pattern := RegEx.new()
	pattern.compile(HEX_PATTERN)
	var offenders := PackedStringArray()
	var scanned := 0
	var directories := PackedStringArray([NO_HEX_ROOT])
	directories.append_array(_hex_extra_dirs)
	for path: String in _gd_files(directories):
		if NO_HEX_EXEMPT.has(path.get_file()):
			continue
		scanned += 1
		for entry: Array in _logical_lines(FileAccess.get_file_as_string(path)):
			var source: String = entry[1]
			if source.strip_edges().begins_with("#"):
				continue
			var hit := pattern.search(source)
			if hit != null:
				offenders.append("%s:%d %s" % [path.get_file(), entry[0], hit.get_string()])
	_record("no screen writes a raw color", offenders.is_empty(),
			"%d file%s scanned%s" % [scanned, "" if scanned == 1 else "s",
			"" if offenders.is_empty() else ", offenders: " + ", ".join(offenders)])


## Every `.gd` under the given directories, recursively. Recursive because a
## `screens/settings/` subdirectory appearing later must not silently drop out of
## the scan and leave the check reporting a green line over fewer files.
static func _gd_files(directories: PackedStringArray) -> PackedStringArray:
	var found := PackedStringArray()
	for directory: String in directories:
		if not DirAccess.dir_exists_absolute(directory):
			continue
		for entry: String in DirAccess.get_files_at(directory):
			if entry.ends_with(".gd"):
				found.append(directory.path_join(entry))
		for entry: String in DirAccess.get_directories_at(directory):
			found.append_array(_gd_files(PackedStringArray([directory.path_join(entry)])))
	return found


## Source split into logical lines: `[first physical line number, joined source]`.
## A trailing backslash continues onto the next line, and GDScript allows it
## anywhere an expression can break — including in the middle of a constructor
## call, which is the case the per-physical-line scan could not see.
static func _logical_lines(text: String) -> Array:
	var out: Array = []
	var raw := text.split("\n")
	var index := 0
	while index < raw.size():
		var first := index + 1
		var joined: String = raw[index]
		while index + 1 < raw.size():
			var right := joined.rstrip(" \t")
			if not right.ends_with("\\"):
				break
			joined = right.substr(0, right.length() - 1) + " " + raw[index + 1]
			index += 1
		out.append([first, joined])
		index += 1
	return out


## Check 11. The hand-off is declared rather than discovered: whatever
## `SessionRequest` can carry is a subset of what `circuit.gd` documents, and the
## two signals the shell binds are on the scene that emits them.
func _check_handoff() -> void:
	if not _wants("handoff"):
		return
	SessionRequest.clear()
	SessionRequest.post({"track": "a", "layout": "reverse", "session": "practice"})
	var posted := SessionRequest.take({})
	var unknown := PackedStringArray()
	for key: String in posted:
		if not CIRCUIT_ARGS.has(key):
			unknown.append(key)
	_record("posted keys are circuit.gd's vocabulary", unknown.is_empty(),
			"%d posted%s" % [posted.size(),
			"" if unknown.is_empty() else ", unknown: " + ", ".join(unknown)])

	# **The read is destructive, and that is the whole safety property.** If this
	# ever passes twice, a stale configuration survives into the next session and
	# the shell is holding simulation state again.
	var second := SessionRequest.take({})
	_record("take() is read-once", second.is_empty(), "%d keys on the second take" % second.size())

	# **And the command line wins.** Get this backwards and drive.sh, circuit.sh
	# and every recorded shoot.sh quietly change meaning while still running.
	SessionRequest.post({"layout": "reverse", "session": "heat"})
	var merged := SessionRequest.take({"layout": "forward"})
	_record("the command line wins a contested key",
			String(merged.get("layout", "")) == "forward"
			and String(merged.get("session", "")) == "heat",
			"layout=%s session=%s" % [merged.get("layout", ""), merged.get("session", "")])

	SessionRequest.deliver({"best_lap_s": 1.0})
	var drained := SessionRequest.take_result()
	var again := SessionRequest.take_result()
	_record("take_result() is read-once", not drained.is_empty() and again.is_empty(),
			"%d then %d keys" % [drained.size(), again.size()])

	var scene := load("res://scenes/game/circuit.tscn") as PackedScene
	if scene == null:
		_record("circuit.tscn exists", false, "did not load")
		return
	var node := scene.instantiate()
	var signals := PackedStringArray()
	for entry: Dictionary in node.get_signal_list():
		signals.append(String(entry["name"]))
	_record("circuit.gd declares session_over", signals.has("session_over"),
			"%d signals" % signals.size())
	_record("circuit.gd declares step_done", signals.has("step_done"),
			"%d signals" % signals.size())
	_record("circuit.gd exposes set_paused", node.has_method("set_paused"), "")
	_record("circuit.gd exposes leave_session", node.has_method("leave_session"), "")
	node.free()


## Check 9d. Every settings row round-trips: move it, `save()`, `load()` into a
## fresh instance under a temp `base_dir`, assert it survived.
##
## **This is the check that kills the decorative slider.** ARCHITECTURE §18's
## comfort options existed as constants in the camera scripts and not in the save
## format, so a settings screen offering them would have been a claim the code
## does not honor. A row that cannot be proved here does not belong on the screen.
##
## Two clauses were missing and both are the same mistake: the check never proved
## it was reading the **file**. `load()`'s `existed` flag went unread, and a field
## whose default already equalled the value written would round-trip whether or
## not anything reached disk — `x * 0.5 + 1.0` is the identity at exactly 2.0, so
## a field defaulting to 2.0 was "moved" to itself.
func _check_settings_round_trip() -> void:
	if not _wants("settings_round_trip"):
		return
	if not ClassDB.class_exists("KartSettings"):
		_record("settings round-trip", false, "KartSettings is not registered")
		return
	var write := KartSettings.new()
	write.set_base_dir(BASE_DIR)
	write.set_auto_clutch(false)
	write.set_auto_shift(false)
	write.set_camera(1)
	write.set_master_volume_db(-6.0)
	write.set_steer_deadzone(0.22)
	var moved := {
		"auto_clutch": false, "auto_shift": false, "camera": 1,
		"master_volume_db": -6.0, "steer_deadzone": 0.22,
	}
	# Anything the C++ side has grown since — the comfort fields and
	# `pause_invalidates_lap` — is picked up by name rather than listed twice, so a
	# new setting is covered by this gate the moment it is bound.
	var unmoved := PackedStringArray()
	for field: String in ["field_of_view_deg", "head_motion", "shake",
			"horizon_lock", "motion_blur", "pause_invalidates_lap"]:
		var setter := "set_" + field
		if not write.has_method(setter):
			continue
		var getter := _getter_for(write, field)
		if getter.is_empty():
			continue
		var current: Variant = write.call(getter)
		var changed: Variant = (not bool(current)) if current is bool \
				else float(current) * 0.5 + 1.0
		if not (current is bool) and absf(float(changed) - float(current)) < 1e-9:
			# The identity case. Nudge it, and say so — a field silently written back
			# to the value it already held is a row this check does not cover.
			changed = float(current) + 1.0
			unmoved.append(field)
		write.call(setter, changed)
		moved[field] = write.call(getter)
	if not unmoved.is_empty():
		_notes.append("settings: %s defaulted to the fixed point of the nudge and were "
				% ", ".join(unmoved) + "moved by +1.0 instead")

	var saved: Dictionary = write.save()
	if not bool(saved.get("ok", false)):
		_record("settings round-trip", false, "save refused: %s" % saved)
		return

	# The negative control, and it is the whole reason to believe the check. A
	# fresh instance that has *not* loaded must disagree with the moved values —
	# otherwise the round trip below would pass on defaults alone, with nothing
	# proving a byte reached disk.
	var untouched := KartSettings.new()
	var defaults_differ := 0
	for field: String in moved:
		var getter := _getter_for(untouched, field)
		if getter.is_empty():
			continue
		var stock: Variant = untouched.call(getter)
		var same: bool = (stock == moved[field]) if stock is bool \
				else absf(float(stock) - float(moved[field])) < 1e-9
		if not same:
			defaults_differ += 1
	_record("the round-trip values differ from the defaults", defaults_differ > 0,
			"%d of %d field%s moved off its default" % [defaults_differ, moved.size(),
			"" if moved.size() == 1 else "s"])

	var read := KartSettings.new()
	read.set_base_dir(_break_settings_dir())
	var reloaded: Dictionary = read.load()
	_record("settings.cfg was found on the reload", bool(reloaded.get("existed", false)),
			"load() reported existed=%s at %s"
			% [reloaded.get("existed", false), read.settings_path()])
	var wrong := PackedStringArray()
	for field: String in moved:
		var getter := _getter_for(read, field)
		if getter.is_empty():
			wrong.append("%s (no getter)" % field)
			continue
		var back: Variant = read.call(getter)
		var same: bool = (back == moved[field]) if back is bool \
				else absf(float(back) - float(moved[field])) < 1e-9
		if not same:
			wrong.append("%s %s != %s" % [field, back, moved[field]])
	_record("every settings row round-trips", wrong.is_empty(),
			"%d field%s%s" % [moved.size(), "" if moved.size() == 1 else "s",
			"" if wrong.is_empty() else ", wrong: " + ", ".join(wrong)])


static func _getter_for(node: Object, field: String) -> String:
	for prefix: String in ["get_", "is_", ""]:
		if node.has_method(prefix + field):
			return prefix + field
	return ""


## Check 12. A best lap survives a reload, and a non-lap writes nothing.
##
## The negative control is the more important half. `result()["best_lap_s"]` is
## `-1.0` when no lap was set, and `lap_timing.h` is emphatic that zero sorts
## first and can never be beaten — so a store that wrote a placeholder would seat
## an undrivable time at the top of the sheet forever.
func _check_best_lap() -> void:
	if not _wants("best_lap"):
		return
	if not ClassDB.class_exists("KartProfile"):
		_record("a best lap survives a reload", false, "KartProfile is not registered")
		return

	var profile := KartProfile.new()
	profile.set_base_dir(BASE_DIR)
	profile.reset_to_fresh()
	var before := profile.best_count()

	if not ResourceLoader.exists("res://scripts/shell/best_lap_store.gd"):
		_record("best_lap_store.gd is built", false, "no script at that path")
	else:
		var store := load("res://scripts/shell/best_lap_store.gd")
		# The negative control first, so a store that writes unconditionally fails
		# here rather than after it has already put a lie in the profile.
		store.call("record", profile, {
			"track": SYNTHETIC_TRACK, "layout": 0, "kart_class": 0,
			"has_best": false, "best_lap_s": -1.0,
		})
		_record("a session with no lap writes nothing", profile.best_count() == before,
				"best_count %d -> %d" % [before, profile.best_count()])
		var outcome: Dictionary = store.call("record", profile, {
			"track": SYNTHETIC_TRACK, "layout": 0, "kart_class": 0,
			"has_best": true, "best_lap_s": SYNTHETIC_BEST,
		})
		_record("a best lap reports that it improved",
				bool(outcome.get("improved", false)) and bool(outcome.get("ok", false)),
				str(outcome))

	# Whether or not the store exists, prove the persistence underneath it: this
	# is the hole the whole push is about, and it must be measured directly.
	var stored := profile.set_best(SYNTHETIC_TRACK, 0, 0, SYNTHETIC_BEST, "probeghost01")
	var written: Dictionary = profile.save()
	if not stored or not bool(written.get("ok", false)):
		_record("a best lap survives a reload", false,
				"set_best %s, save refused: %s" % [stored, profile.save_block_reason()])
	else:
		var reloaded := KartProfile.new()
		reloaded.set_base_dir(BASE_DIR)
		reloaded.load()
		var read := reloaded.best_time(SYNTHETIC_TRACK, 0, 0)
		# `%.9f` and not `%.3g`. GDScript's `%` has no `%g` and no `%e` — it
		# leaves the literal in the string rather than erroring, which CLAUDE.md's
		# trap list already records for `%e`. The same hole swallows `%g`.
		_record("a best lap survives a reload", absf(read - SYNTHETIC_BEST) < 1e-9,
				"wrote %.6f, read %.6f, delta %.9f" % [SYNTHETIC_BEST, read,
				absf(read - SYNTHETIC_BEST)])

	# **A best lap set with the ghost turned off.** Separate, because it does not
	# work and the reason is structural rather than a missing caller:
	# `profile_is_slug("")` is false on `len <= 0`, so `set_best` refuses an empty
	# ghost id — and `is_valid()` and the record parser agree with it, all three.
	# The `best` record has no spelling for "no ghost".
	#
	# The setup screen offers "Ghost: off / personal best", so this is reachable
	# from a menu on the first session anybody drives, and the symptom is a lap
	# time that silently does not save. It is C++ work with a format decision in
	# it — `-` is the natural sentinel because a slug may not begin with one — and
	# it stays red here until the format has one.
	var no_ghost := KartProfile.new()
	no_ghost.set_base_dir(BASE_DIR)
	no_ghost.reset_to_fresh()
	var without := no_ghost.set_best(SYNTHETIC_TRACK, 0, 0, SYNTHETIC_BEST, "")
	_record("a best lap with no ghost can be recorded", without,
			"set_best with an empty ghost id returned %s, best_count %d"
			% [without, no_ghost.best_count()])


## Check 14. Ghost round-trip: record, save under an id, load it back, and assert
## the pose agrees and the id is what `set_best` stored.
func _check_ghost() -> void:
	if not _wants("ghost"):
		return
	if not ClassDB.class_exists("KartGhost"):
		_record("a ghost round-trips", false, "KartGhost is not registered")
		return
	var ghost := KartGhost.new()
	# **Not the real circuit's slug.** `KartGhost.ghost_directory()` is a fixed
	# `user://ghosts` with no `set_base_dir()` to redirect it, and the id is
	# derived from track + layout + class — so recording under `valdirone_nuova`
	# would overwrite Anthony's own personal-best ghost. This slug exists nowhere
	# else, and `_scrub()` deletes what it writes.
	ghost.set_track(GHOST_TRACK)
	ghost.set_layout(0)
	ghost.set_kart_class(0)
	ghost.begin_record()
	# A straight run at a constant speed, so `transform_at_time` has an analytic
	# answer to be checked against rather than a recorded one to agree with.
	#
	# **Fed at the physics rate, not at `sample_hz()`.** `record_tick` is called
	# once per physics tick and decimates internally — `ghost_probe.gd` does the
	# same — so spacing the poses by `1 / sample_hz` instead makes the ghost store
	# every fourth position while labelling them consecutive, and it plays back at
	# four times the speed it was recorded at. Measured that way once, and the
	# figure it produces looks like a plausible ghost rather than a bug.
	var hz := float(Engine.physics_ticks_per_second)
	var ticks := int(hz * 2.0)
	for tick: int in ticks + 1:
		var t := float(tick) / hz
		ghost.record_tick(Transform3D(Basis(), Vector3(0.0, 0.0, -GHOST_SPEED * t)))
	var lap := float(ticks) / hz
	if not ghost.finish_record(lap, PackedFloat64Array([lap / 3.0, lap / 3.0, lap / 3.0])):
		_record("a ghost round-trips", false, "finish_record refused %d ticks" % ticks)
		return

	var id := ghost.id()
	if ghost.save_as_id() != OK:
		_record("a ghost round-trips", false, "save_as_id refused for %s" % id)
		return
	var back := KartGhost.new()
	var loaded: Dictionary = back.load_id(id)
	if not bool(loaded.get("ok", false)):
		_record("a ghost round-trips", false, "load_id refused: %s" % loaded)
		return
	var at := lap * 0.5
	var pose: Transform3D = back.transform_at_time(at)
	var expected := -GHOST_SPEED * at
	_record("a ghost round-trips", absf(pose.origin.z - expected) < 0.05,
			"id %s, at %.3f s: z %.3f, analytic %.3f, %d samples" % [
			id, at, pose.origin.z, expected, back.sample_count()])
	_ghost_id = id


# --- the sabotages ----------------------------------------------------------------
#
# Every one of these is three lines and does exactly one thing. Nothing here
# writes to the repo, and nothing survives the process: the InputMap is
# per-process, the tree is thrown away, and `user://shell_probe/break/` is
# scrubbed with everything else.


func _break_overlap() -> void:
	if not _broke("overlap"):
		return
	# `respawn` already carries pad:1. Adding pad:0 makes it collide with
	# `menu_confirm` as well, which is an overlap nobody declared.
	var event := InputEventJoypadButton.new()
	event.button_index = JOY_BUTTON_A
	InputMap.action_add_event(&"respawn", event)


func _break_hex() -> void:
	if not _broke("hex"):
		return
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(BREAK_DIR))
	var file := FileAccess.open(BREAK_DIR + "offender.gd", FileAccess.WRITE)
	if file == null:
		return
	# Two holes in one file: a stock color constant, which the original pattern did
	# not know about, and a constructor split over a continuation, which a
	# per-physical-line scan cannot see on either half.
	file.store_string("extends Control\n\n"
			+ "const A := Color.CRIMSON\n"
			+ "const B := Color \\\n\t\t(0.1, 0.2, 0.3)\n")
	file.close()
	_hex_extra_dirs.append(BREAK_DIR)


func _break_settings_dir() -> String:
	if not _broke("settings"):
		return BASE_DIR
	# The reload looks in a directory nothing was ever written to, so every field
	# comes back at its default. If the round-trip still passes, it was passing on
	# defaults and never read the file at all.
	return BASE_DIR + "elsewhere/"


func _break_sim() -> void:
	if not _broke("sim"):
		return
	# `PlayerDriver` is the cheapest simulation Node in the extension: `extends
	# Node`, no physics, no scene requirements.
	if not ClassDB.class_exists("PlayerDriver"):
		_notes.append("--break=sim: PlayerDriver is not registered, nothing to plant")
		return
	var planted: Node = ClassDB.instantiate("PlayerDriver")
	planted.name = "PlantedDriver"
	_shell.add_child(planted)


func _break_pause() -> void:
	if not _broke("pause"):
		return
	var layer := _shell.get_node_or_null("UI")
	if layer != null:
		layer.process_mode = Node.PROCESS_MODE_INHERIT


func _break_paddock() -> void:
	var top := _stack.top()
	if top == null:
		return
	if _broke("focus"):
		# The screen is moved bodily off the canvas. Focus is still on a real
		# control with a real rect; the rect is just somewhere nobody can see.
		top.position = Vector2(-5000.0, -5000.0)
	if _broke("occlude"):
		# An opaque full-bleed panel added **last**, so it draws over everything
		# including the focused row.
		top.add_child(ShellTheme.ground(false))
	if _broke("reach"):
		# One extra focusable, and every existing control's four neighbors pinned to
		# itself so the walk cannot leave where it started. Pinning is deterministic;
		# placing the orphan far away is not — `find_valid_focus_neighbor` was
		# measured finding a control 1,400 px away and diagonally off the band.
		for control: Control in top.focusables():
			for side: String in ["top", "bottom", "left", "right"]:
				control.set("focus_neighbor_" + side, control.get_path())
		var orphan := ShellTheme.row_button("orphan", ShellTheme.PAPER_INK)
		orphan.position = Vector2(1200.0, 60.0)
		orphan.size = Vector2(120.0, 24.0)
		top.add_child(orphan)


# --- reporting -------------------------------------------------------------------


func _record(name: String, ok: bool, measurement: String) -> void:
	_checks.append([name, ok, measurement])


func _fail(name: String, measurement: String) -> void:
	_record(name, false, measurement)


func _top_name() -> String:
	var top := _stack.top() if _stack != null else null
	return top.title() if top != null else "<none>"


func _focus_name() -> String:
	var focused := root.gui_get_focus_owner()
	return focused.name if focused != null else "<none>"


func _focus_inside(screen: Node) -> bool:
	var focused := root.gui_get_focus_owner()
	return focused != null and screen != null and screen.is_ancestor_of(focused)


static func _floats(values: PackedFloat32Array) -> PackedStringArray:
	var out := PackedStringArray()
	for value: float in values:
		out.append("%.0f" % value)
	return out


func _report() -> void:
	var failures := 0
	for check: Array in _checks:
		var ok: bool = check[1]
		failures += 0 if ok else 1
		print("check %-52s %s   %s" % [check[0], "PASS" if ok else "FAIL", check[2]])
	for note: String in _notes:
		print("note  %s" % note)
	# The case list, always. A subset run that prints "12 of 12 passed" reads
	# exactly like a full one, and somebody will paste it into a ticket.
	print("cases: %s" % ("all %d" % CASES.size() if _cases == CASES
			else "%d of %d — %s" % [_cases.size(), CASES.size(), ", ".join(_cases)]))
	print("shell probe: %d of %d checks passed%s" % [
		_checks.size() - failures, _checks.size(),
		"" if failures == 0 else ", %d FAILED" % failures,
	])

	if not Cmdline.as_bool(_args, "keep", false):
		_scrub()

	if _break.is_empty():
		quit(0 if failures == 0 else 1)
		return

	# **Inverted, and see the header.** The sabotage was aimed at one check by
	# name; that check going red is the negative control passing.
	var target: String = BREAK_MODES[_break][0]
	var evidence: String = BREAK_MODES[_break][1]
	var ran := 0
	var red := 0
	var caught := PackedStringArray()
	for check: Array in _checks:
		if not String(check[0]).begins_with(target):
			continue
		ran += 1
		if bool(check[1]):
			continue
		red += 1
		# Red is necessary and, where the sabotage signs its work, not sufficient.
		if evidence.is_empty() or String(check[2]).contains(evidence):
			caught.append(String(check[0]))
	if ran == 0:
		print("negative control --break=%s: NOT CAUGHT — no check named \"%s...\" ran, "
				% [_break, target] + "so the sabotage was aimed at nothing")
		quit(1)
		return
	print("negative control --break=%s: %s — %d of %d \"%s...\" check%s went red%s, "
			% [_break, "caught" if not caught.is_empty() else "NOT CAUGHT",
			red, ran, target, "" if ran == 1 else "s",
			"" if evidence.is_empty() else ", %d naming \"%s\"" % [caught.size(), evidence]]
			+ "%d of %d red overall" % [failures, _checks.size()])
	quit(0 if not caught.is_empty() else 1)


## Everything this probe wrote, removed. `profile_probe.gd`'s rule: a gate that
## left a synthetic career behind would be a worse bug than the one it looks for.
func _scrub() -> void:
	if not _ghost_id.is_empty():
		# The one thing this probe writes outside its own base dir, because
		# `KartGhost` has no `set_base_dir()` to point somewhere else.
		var ghost_path: String = KartGhost.path_for_id(_ghost_id)
		if FileAccess.file_exists(ghost_path):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(ghost_path))
	_remove_tree(ProjectSettings.globalize_path(BASE_DIR))


## `BASE_DIR` and everything under it. Recursive, because `--break=hex` and
## `--break=settings` both write into subdirectories and a flat scrub would leave
## a `break/` behind for the next run's regex to find.
static func _remove_tree(absolute: String) -> void:
	if not DirAccess.dir_exists_absolute(absolute):
		return
	for entry: String in DirAccess.get_directories_at(absolute):
		_remove_tree(absolute.path_join(entry))
	for entry: String in DirAccess.get_files_at(absolute):
		DirAccess.remove_absolute(absolute.path_join(entry))
	DirAccess.remove_absolute(absolute)
