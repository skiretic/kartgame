extends SceneTree

## The shell's structural gate. ADR-0053 §3: **numbers gate structure and
## Anthony's eye gates looks**, so nothing here judges a layout — it asserts that
## every screen is reachable, that back returns where it came from, that focus
## lands somewhere a person can see, that a pad alone can reach every control,
## and that a lap time survives a relaunch.
##
##     godot --headless --path . --script tools/verify/shell_probe.gd
##       --case=<name>[,<name>]   run a subset; default is all of them
##       --keep                   leave user://shell_probe/ behind for inspection
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
## House style: named checks, PASS or FAIL **with the measurement either way**,
## and a non-zero exit.

const SHELL_SCENE := "res://scenes/shell/shell.tscn"
const BASE_DIR := "user://shell_probe/"

## The design canvas, from `project.godot`. See the header.
const VIEWPORT := Vector2i(1600, 900)

## Every case, in the order they run. `--case=` picks a subset.
const CASES: PackedStringArray = [
	"main_scene", "reachable", "back", "focus", "pad_reach", "no_trap",
	"actions", "overlap", "no_hex", "no_sim", "one_focus", "settings_round_trip",
	"theme", "handoff", "best_lap", "results", "ghost",
]

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

## Check 9b's forbidden list. GAMEDESIGN §9's sentence, as an assertion.
const SIMULATION_CLASSES: PackedStringArray = [
	"KartBody", "PlayerDriver", "SessionRunner", "KartTuning", "KartLapTimer",
]

## Check 9's subject: the directories where a raw color literal is a bug.
const NO_HEX_DIRS: PackedStringArray = [
	"res://scripts/shell/screens/", "res://scripts/shell/widgets/",
]

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

var _args := {}
var _cases: PackedStringArray = []
var _checks: Array = []
var _shell: Node
var _stack: ScreenStack
var _frames := 0
var _phase := 0
var _notes := PackedStringArray()
var _ghost_id := ""


func _initialize() -> void:
	_args = Cmdline.parse()
	var wanted := Cmdline.as_string(_args, "case", "")
	_cases = CASES if wanted.is_empty() else wanted.split(",", false)

	# **Force the shipped 16:9 canvas.** Headless, the window collapses to its
	# 64x64 minimum and `aspect="expand"` then expands the 1600-wide canvas to
	# 1600x1600 to match it — measured, and it is why a control anchored to the
	# bottom of the screen lands 640 px below the real viewport's floor and a
	# naive on-screen check passes it. `IGNORE` pins the canvas to exactly the
	# design size, which is what `expand` produces in the shipped 16:9 window.
	root.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_IGNORE
	root.content_scale_size = VIEWPORT

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
	_shell.set("arg_override", {"backdrop": "flat"})
	root.add_child(_shell)


func _needs_scene() -> bool:
	for name: String in ["reachable", "back", "focus", "pad_reach", "no_trap",
			"no_sim", "one_focus", "theme", "results"]:
		if _cases.has(name):
			return true
	return false


# --- the walk ------------------------------------------------------------------
#
# One phase per frame, because push() lands focus a frame later on purpose:
# grab_focus() on a node that is not yet laid out puts focus on a zero-sized rect,
# which is exactly what check 4 exists to catch and would otherwise cause.


func _process(_delta: float) -> bool:
	_frames += 1
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
			_check_no_sim()
			_check_theme()
			_walk_screens()
		1:
			_check_focus_here("boot")
			_check_pad_reach_here("boot")
			_check_one_focus()
			var boot := _stack.top()
			if boot != null and boot.has_method("_advance"):
				boot.call("_advance")
		2:
			_check_here("paddock")
			_check_focus_here("paddock")
			_check_pad_reach_here("paddock")
			_check_no_trap_here("paddock")
			_walk_optional_screens()
		3:
			_check_results()
		_:
			_report()
			return true
	_phase += 1
	return false


## Check 2, the reachable half: the stack starts at depth 1 on boot.
func _walk_screens() -> void:
	_record("stack opens at depth 1", _stack.depth() == 1,
			"depth %d, top %s" % [_stack.depth(), _top_name()])
	_check_here("boot")


## Checks 2 and 3 over every screen the paddock can reach: push it, assert the
## depth and the class, pop it, assert we are back at the **same object**.
##
## Same object and not same class: a pop that rebuilt the paddock would look
## identical in a class check and would have thrown away every bit of state the
## screen was holding.
func _walk_optional_screens() -> void:
	var shell_root := _shell
	for key: String in ["setup", "settings", "profile", "results", "loading", "pause"]:
		if not bool(shell_root.call("has_screen", key)):
			_record("screen %s is built" % key, false, "no script at that path")
			continue
		var before := _stack.top()
		var depth := _stack.depth()
		var opened: bool = shell_root.call("open", key)
		if not opened:
			_record("screen %s opens" % key, false, "open() refused")
			continue
		_record("screen %s opens" % key, _stack.depth() == depth + 1,
				"depth %d -> %d, top %s" % [depth, _stack.depth(), _top_name()])
		var popped := _stack.back()
		_record("back from %s returns to the same object" % key,
				popped and _stack.top() == before and _stack.depth() == depth,
				"depth %d, same object %s" % [_stack.depth(), _stack.top() == before])


func _check_here(expected: String) -> void:
	if not _cases.has("reachable"):
		return
	var top := _stack.top()
	_record("top screen is %s" % expected, top != null and top.title() == expected,
			"top is %s" % _top_name())


## Check 4. Focus is non-null, a descendant of the top screen, visible in tree,
## **and its global rect intersects the viewport** — the clause a naive check
## misses, and the one that catches a control focused before its container sorted.
func _check_focus_here(screen: String) -> void:
	if not _cases.has("focus"):
		return
	var top := _stack.top()
	if top == null:
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


## Check 5. BFS over `find_valid_focus_neighbor()` on four sides from the entry
## control must reach every `FOCUS_ALL` descendant. **Names the orphan**, because
## "some control is unreachable" is not a sentence anybody can act on.
func _check_pad_reach_here(screen: String) -> void:
	if not _cases.has("pad_reach"):
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

	var seen := {start: true}
	var queue: Array[Control] = [start]
	while not queue.is_empty():
		var from: Control = queue.pop_front()
		for side: int in [SIDE_TOP, SIDE_BOTTOM, SIDE_LEFT, SIDE_RIGHT]:
			var next := from.find_valid_focus_neighbor(side)
			if next != null and not seen.has(next) and top.is_ancestor_of(next):
				seen[next] = true
				queue.append(next)

	var orphans := PackedStringArray()
	for control: Control in all:
		if not seen.has(control):
			orphans.append("%s(%s)" % [control.name, control.get_class()])
	_record("pad reaches every control on %s" % screen, orphans.is_empty(),
			"%d of %d reached%s" % [seen.size(), all.size(),
			"" if orphans.is_empty() else ", orphans: " + ", ".join(orphans)])


## Check 6. `menu_back` pops, from every focusable on the screen.
##
## Exempt where `can_pop()` is false — the paddock is the bottom of the stack and
## boot is not somewhere you return from, so refusing is the correct behavior and
## the check asserts the refusal instead.
func _check_no_trap_here(screen: String) -> void:
	if not _cases.has("no_trap"):
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
	if not _cases.has("one_focus"):
		return
	var holders := PackedStringArray()
	for node: Node in root.find_children("*", "Control", true, false):
		if (node as Control).has_focus():
			holders.append(node.name)
	_record("exactly one control has focus", holders.size() == 1,
			"%d: %s" % [holders.size(), ", ".join(holders)])


## Check 9b. No simulation state anywhere under `ShellRoot`.
func _check_no_sim() -> void:
	if not _cases.has("no_sim"):
		return
	var found := PackedStringArray()
	for class_name_: String in SIMULATION_CLASSES:
		if not ClassDB.class_exists(class_name_):
			continue
		for node: Node in _shell.find_children("*", class_name_, true, false):
			found.append("%s(%s)" % [node.name, class_name_])
	_record("no simulation state under ShellRoot", found.is_empty(),
			"%d found%s" % [found.size(),
			"" if found.is_empty() else ": " + ", ".join(found)])


## Check 10. The theme resolves, and **says which font path it used**. A fallback
## is not a failure on a fresh clone — the TTFs are gitignored and reproduced by
## a fetch script — but a still shot on the wrong face has to be diagnosable from
## the log rather than by eye.
func _check_theme() -> void:
	if not _cases.has("theme"):
		return
	var source := ShellTheme.font_source()
	var face := ShellTheme.face(ShellTheme.Weight.BOLD, 4)
	var width := face.get_string_size("0:46.611", HORIZONTAL_ALIGNMENT_LEFT, -1.0,
			int(ShellTheme.T_HERO)).x
	_record("theme resolves a face", face != null and width > 0.0,
			"font %s, 0:46.611 at %d px is %.1f px wide" % [
			source, int(ShellTheme.T_HERO), width])
	_notes.append("font source: %s" % source)

	var scale := ShellTheme.scale_for(Vector2(VIEWPORT))
	_record("type scale is 1.0 at the design height", absf(scale - 1.0) < 1e-6,
			"%.4f at %d px" % [scale, VIEWPORT.y])


## Check 13. The results screen renders the ledger it is handed: 12 synthetic
## rows, 2 struck, and the pinned best equals `min` over the **valid** rows only.
func _check_results() -> void:
	if not _cases.has("results"):
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
	if not _cases.has("main_scene"):
		return
	var main := String(ProjectSettings.get_setting("application/run/main_scene", ""))
	_record("run/main_scene is the shell", main == SHELL_SCENE, main)


## Check 7. #169's reader check, applied to menus: the six actions exist, and each
## has at least one joypad event — a menu bound to the keyboard alone is a menu a
## driver holding a pad cannot use, which is exactly the failure that milestone
## shipped.
func _check_actions() -> void:
	if not _cases.has("actions"):
		return
	for action: String in MENU_ACTIONS:
		if not InputMap.has_action(action):
			_record("action %s exists" % action, false, "absent from the InputMap")
			continue
		var pads := 0
		for event: InputEvent in InputMap.action_get_events(action):
			if event is InputEventJoypadButton or event is InputEventJoypadMotion:
				pads += 1
		_record("action %s has a pad binding" % action, pads >= 1,
				"%d joypad event%s" % [pads, "" if pads == 1 else "s"])


## Check 8. The overlap between menu and driving bindings equals the declared set.
func _check_overlap() -> void:
	if not _cases.has("overlap"):
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
## file permitted a hex, and this is the regex that keeps it that way — because a
## theme with one hardcoded blue in it is a theme the livery round cannot move.
func _check_no_hex() -> void:
	if not _cases.has("no_hex"):
		return
	var pattern := RegEx.new()
	pattern.compile("Color\\s*\\(|Color8\\s*\\(|\"#[0-9a-fA-F]{3,8}\"")
	var offenders := PackedStringArray()
	var scanned := 0
	for directory: String in NO_HEX_DIRS:
		for path: String in _gd_files(directory):
			scanned += 1
			var text := FileAccess.get_file_as_string(path)
			var line := 0
			for source: String in text.split("\n"):
				line += 1
				var trimmed := source.strip_edges()
				if trimmed.begins_with("#") or trimmed.begins_with("##"):
					continue
				if pattern.search(source) != null:
					offenders.append("%s:%d" % [path.get_file(), line])
	_record("no screen writes a raw color", offenders.is_empty(),
			"%d file%s scanned%s" % [scanned, "" if scanned == 1 else "s",
			"" if offenders.is_empty() else ", offenders: " + ", ".join(offenders)])


static func _gd_files(directory: String) -> PackedStringArray:
	var found := PackedStringArray()
	if not DirAccess.dir_exists_absolute(directory):
		return found
	for entry: String in DirAccess.get_files_at(directory):
		if entry.ends_with(".gd"):
			found.append(directory + entry)
	return found


## Check 11. The hand-off is declared rather than discovered: whatever
## `SessionRequest` can carry is a subset of what `circuit.gd` documents, and the
## two signals the shell binds are on the scene that emits them.
func _check_handoff() -> void:
	if not _cases.has("handoff"):
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
func _check_settings_round_trip() -> void:
	if not _cases.has("settings_round_trip"):
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
		write.call(setter, changed)
		moved[field] = write.call(getter)

	var saved: Dictionary = write.save()
	if not bool(saved.get("ok", false)):
		_record("settings round-trip", false, "save refused: %s" % saved)
		return

	var read := KartSettings.new()
	read.set_base_dir(BASE_DIR)
	read.load()
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
	if not _cases.has("best_lap"):
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
	if not _cases.has("ghost"):
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


# --- reporting -------------------------------------------------------------------


func _record(name: String, ok: bool, measurement: String) -> void:
	_checks.append([name, ok, measurement])


func _fail(name: String, measurement: String) -> void:
	_record(name, false, measurement)


func _top_name() -> String:
	var top := _stack.top() if _stack != null else null
	return top.title() if top != null else "<none>"


func _report() -> void:
	var failures := 0
	for check: Array in _checks:
		var ok: bool = check[1]
		failures += 0 if ok else 1
		print("check %-48s %s   %s" % [check[0], "PASS" if ok else "FAIL", check[2]])
	for note: String in _notes:
		print("note  %s" % note)
	print("shell probe: %d of %d checks passed%s" % [
		_checks.size() - failures, _checks.size(),
		"" if failures == 0 else ", %d FAILED" % failures,
	])
	if not Cmdline.as_bool(_args, "keep", false):
		_scrub()
	quit(0 if failures == 0 else 1)


## Everything this probe wrote, removed. `profile_probe.gd`'s rule: a gate that
## left a synthetic career behind would be a worse bug than the one it looks for.
func _scrub() -> void:
	if not _ghost_id.is_empty():
		# The one thing this probe writes outside its own base dir, because
		# `KartGhost` has no `set_base_dir()` to point somewhere else.
		var ghost_path: String = KartGhost.path_for_id(_ghost_id)
		if FileAccess.file_exists(ghost_path):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(ghost_path))

	var absolute := ProjectSettings.globalize_path(BASE_DIR)
	if not DirAccess.dir_exists_absolute(absolute):
		return
	for entry: String in DirAccess.get_files_at(absolute):
		DirAccess.remove_absolute(absolute.path_join(entry))
	DirAccess.remove_absolute(absolute)
