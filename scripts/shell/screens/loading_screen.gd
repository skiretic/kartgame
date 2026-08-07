extends ShellScreen

## Plate 7. The screen-document family: what is being built, how far in, and one
## measured fact.
##
## ## The one thing this screen must not do
##
## **`circuit.gd` builds synchronously in `_ready()`.** All eleven steps run inside
## one blocked call, so the eleven `step_done` signals arrive in one burst with no
## frame between them, and a bar that swept smoothly through named stages would be
## the only lie on the screen. Measured on this machine, headless, over three runs
## of `scenes/game/circuit.tscn` against Valdirone Nuova:
##
##     track          0.00 ms   <- see `_step()`; the first step measures itself
##     environment    0.80 / 0.81 / 0.82 ms
##     ground       284.23 / 280.45 / 280.84 ms
##     collision      9.46 /   7.87 /   7.93 ms
##     scatter      895.09 / 875.89 / 890.82 ms
##     lightmap       0.01 ms
##     kart          22.37 /  21.48 /  22.45 ms
##     tuning         0.17 /   0.13 /   0.15 ms
##     session        0.05 /   0.06 /   0.04 ms
##     cameras        0.30 /   0.29 /   0.30 ms
##     hud            3.18 /   3.11 /   3.11 ms
##     summed      1215.65 / 1190.10 / 1206.47 ms
##
## So the build is ~1.2 s and **97% of it is two steps** — the terrain height field
## and the 5,187-instance scatter. That is the number that decides whether loading
## ever needs to become asynchronous, and it is here rather than in a report
## because the next person to look at this screen is the person who needs it.
##
## The step list is therefore drawn as a **static ledger**: the eleven real names
## from `circuit.gd.BUILD_STEPS`, with the ones that have reported marked. It is
## honest at 0 of 11 and honest at 11 of 11 and it never animates in between,
## because there is no in between.
##
## ## Do not invent the mockup's six step names
##
## The plate reads `track · collision · kart · ghost · audio · session`. Three of
## those six are not stages of anything: there is no ghost stage, no audio stage,
## and `circuit.gd`'s own header says in as many words not to write them here. The
## eleven below are read from the constant, not copied, so a step added to the
## scene appears here without an edit.
##
## ## What this screen cannot do yet, and it is not this file's to fix
##
## `ShellRoot.start_session()` pushes this screen and then calls
## `change_scene_to_file()` in the same call. The scene swap is deferred to the end
## of that frame, which is *before* the frame draws — measured, see the report — so
## as things stand the screen is built, never drawn, and freed. The hooks below
## (`bind_build`, `set_request`, `show_refusal`) are the whole interface a host
## needs to drive it; nothing in the shell calls them yet and the report says which
## two lines change that. They are not decoration: `_probe`'s measurements drive
## every one of them.

const CIRCUIT_SCRIPT := "res://scripts/game/circuit.gd"

## `circuit.gd`'s `--track` default, so a screen opened with nothing posted names
## the circuit the session would actually build rather than a placeholder.
const DEFAULT_TRACK := "res://data/tracks/valdirone_nuova.track.json"

## The three states this screen has. There is no fourth and there is no spinner.
enum Phase { QUEUED, BUILDING, REFUSED }

## The tip corpus. ADR-0052: **measured facts only**, and the row is dropped
## entirely if the pool is thin — which is why there are three and not thirty.
## Each is a figure somebody in this repo measured, with the file it came from, and
## a fourth is added by finding a fourth measurement rather than by writing one.
##
## Not a random pick per session: the index is derived from the day, so the same
## launch shows the same tip and a screenshot is reproducible from its date.
const TIPS: Array = [
	# docs/ROADMAP.md:468, the M5 acceptance argument. The plate's own example.
	"Il Ciglione would demand 3.66 g of the centerline. Nobody drives the "
	+ "centerline — the racing line is why the corner is possible.",
	# docs/ARCHITECTURE.md:207 and ADR-0034. 2.4336 g left, 2.81 g right; the
	# asymmetry is 27 kg of engine, exhaust and radiator 41 mm right of center.
	"This kart tips at 2.43 g turning left and 2.81 g turning right. The engine "
	+ "sits 41 mm off the centerline, so the two directions are not the same corner.",
	# docs/circuits/valdirone_nuova.json:46, T2 Lama's design rationale.
	"T2's apex kerb is struck at 129 km/h. It is on the shortest path at 1.86 g, "
	+ "which is the grip ceiling — the kerb is not optional there.",
]

## Every state this screen shows is one of these, and each is a whole authored
## sentence rather than a fragment assembled from a condition. ADR-0044 rule 1.
const STATUS_QUEUED := "Waiting for the session to start."
const STATUS_BUILDING := "Building the circuit. The process is blocked until it finishes."
const STATUS_DONE := "Built. Handing over to the session."

var _phase := Phase.QUEUED
var _request := {}
var _target_cache := {}
var _steps: PackedStringArray = []
var _done := 0
var _total := 0
var _elapsed_ms := 0.0
var _refusal := ""

var _heading: Label
var _sub: Label
var _status: Label
var _stage: Label
var _counter: Label
var _ledger: Label
var _back: Button


# --- the host's interface -------------------------------------------------------
#
# Three calls, and a host needs at most all three. They are separate because they
# arrive at different times: the request is known before the scene swap, the build
# reports itself during, and a refusal is the answer instead of a build.


## What is being loaded. `circuit.gd`'s own argument vocabulary — `track`,
## `layout`, `session` — because a menu and a shell script say the same thing the
## same way, and `shell_probe.gd` check 11 asserts that set.
##
## Safe to call before or after `build()`.
func set_request(request: Dictionary) -> void:
	_request = request.duplicate()
	_target_cache = {}
	_refresh()


## Watch a circuit build. The node must declare `step_done(name, index, total,
## elapsed_ms)`; anything else is a caller that thinks it configured something, so
## it is refused loudly rather than connected to nothing.
func bind_build(circuit: Node) -> void:
	if circuit == null:
		return
	if not circuit.has_signal("step_done"):
		push_error("%s declares no step_done; the loading screen has nothing to watch"
				% circuit.get_class())
		return
	circuit.connect("step_done", _on_step_done)
	_phase = Phase.BUILDING
	_refresh()


## The session was refused. `SessionRunner.refusal()` is the sentence, verbatim.
##
## **Never a black screen** — that is this method's whole reason to exist. Three of
## the runner's refusals are reachable from a menu (a class nothing simulates, a
## tick-rate mismatch, an entry list longer than one), and every one of them
## currently ends with `push_error` in a scene the player is staring at.
func show_refusal(reason: String) -> void:
	_refusal = reason.strip_edges()
	_phase = Phase.REFUSED
	_refresh()


## What the screen is showing, for a probe. `queued`, `building` or `refused`.
func phase_name() -> String:
	return ["queued", "building", "refused"][_phase]


## How many steps have reported, and out of how many. For a probe.
func progress() -> Vector2i:
	return Vector2i(_done, _total)


# --- construction ---------------------------------------------------------------


func build() -> void:
	_steps = _build_steps()
	_total = _steps.size()

	var page := ScreenPanel.new()
	add_child(page)

	var center := CenterContainer.new()
	page.field().add_child(center)

	var stack := VBoxContainer.new()
	stack.alignment = BoxContainer.ALIGNMENT_CENTER
	stack.add_theme_constant_override("separation", 6)
	center.add_child(stack)

	_heading = ShellTheme.label("", ShellTheme.T_TITLE, ShellTheme.SCR_INK,
			ShellTheme.Weight.BOLD)
	_heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	stack.add_child(_heading)

	_sub = ShellTheme.kicker("", ShellTheme.SCR_SOFT)
	_sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	stack.add_child(_sub)

	# The authored sentence, in sentence case and not as a kicker. A kicker is
	# uppercase and tracked at 0.13em, which is a *label* style; setting a whole
	# sentence in it turns prose into a row of stamps. ADR-0044's sentences are
	# prose and are set as prose.
	_status = ShellTheme.label("", ShellTheme.T_BODY, ShellTheme.SCR_SOFT)
	_status.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.custom_minimum_size = Vector2(520.0, 0.0)
	stack.add_child(_status)

	# The refusal's way out. Built once and hidden, rather than added on demand,
	# because `initial_focus()` runs one frame after `build()` and a control that
	# does not exist yet cannot take focus — which presents as "the pad does
	# nothing" and is the exact failure `shell_screen.gd`'s header warns about.
	_back = ShellTheme.row_button("Back to setup", ShellTheme.SCR_INK, ShellTheme.T_BODY)
	_back.pressed.connect(_leave)
	_back.visible = false
	_back.focus_mode = Control.FOCUS_NONE
	stack.add_child(_back)

	var progress_bar := page.add_bar()
	_stage = ShellTheme.kicker("", ShellTheme.SCR_INK)
	_stage.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	progress_bar.add_child(_stage)
	# The count is the one accent thing on the screen, and it is accent because it
	# is the only number that changes.
	_counter = ShellTheme.kicker("", ShellTheme.ACCENT)
	progress_bar.add_child(_counter)

	var ledger_bar := page.add_bar()
	_ledger = ShellTheme.kicker("", ShellTheme.SCR_SOFT)
	_ledger.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_ledger.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	ledger_bar.add_child(_ledger)

	var tip := _tip()
	if not tip.is_empty():
		var tip_bar := page.add_bar()
		# Not a kicker. A kicker is uppercase and tracked, which is a label style;
		# this is a sentence and the plate sets it back to sentence case at 0.74rem
		# with almost no tracking for exactly that reason.
		var line := ShellTheme.label(tip, ShellTheme.T_FOOT, ShellTheme.SCR_SOFT)
		line.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		tip_bar.add_child(line)

	_refresh()


## The eleven real step names, read off `circuit.gd` rather than copied.
##
## A constant read through `GDScript.get_script_constant_map()` and not a
## `preload().BUILD_STEPS`: `circuit.gd` is a scene script that pulls in
## `KartTrack`, `KartRig` and the whole vehicle stack, and a menu screen has no
## business loading any of it to read a list of strings. Falls back to an empty
## list, which renders as "the step list is unavailable" rather than as a
## confident zero.
func _build_steps() -> PackedStringArray:
	var script := load(CIRCUIT_SCRIPT) as GDScript
	if script == null:
		push_warning("%s did not load; the loading screen has no step list" % CIRCUIT_SCRIPT)
		return PackedStringArray()
	var constants := script.get_script_constant_map()
	if not constants.has("BUILD_STEPS"):
		push_warning("%s declares no BUILD_STEPS" % CIRCUIT_SCRIPT)
		return PackedStringArray()
	return PackedStringArray(constants["BUILD_STEPS"])


# --- state ------------------------------------------------------------------------


func _on_step_done(step_name: String, index: int, total: int, elapsed_ms: float) -> void:
	_phase = Phase.BUILDING
	# `index + 1` and not a counter of our own: the signal carries its own position,
	# and a step that fires twice must not be able to report twelve of eleven.
	_done = maxi(_done, index + 1)
	_total = maxi(_total, total)
	_elapsed_ms += elapsed_ms
	if _steps.is_empty() and total > 0:
		_steps.resize(total)
	if index >= 0 and index < _steps.size():
		_steps.set(index, step_name)
	_refresh()


func _refresh() -> void:
	if _heading == null:
		return

	var target := _target()
	_heading.text = target["name"]
	_sub.text = ("%s · %s" % [target["layout"], target["kart_class"]]).to_upper()

	if _phase == Phase.REFUSED:
		# The refusal replaces the progress, because there is no progress: the
		# session was never configured. The sentence is the runner's own, verbatim.
		_status.add_theme_color_override("font_color", ShellTheme.ST_SLOWER)
		_status.text = _refusal if not _refusal.is_empty() \
				else "SessionRunner.refusal() was empty, which is itself the bug."
		_stage.text = "THE SESSION WAS REFUSED"
		_stage.add_theme_color_override("font_color", ShellTheme.ST_SLOWER)
		_counter.text = ""
		_ledger.text = _ledger_text()
		_back.visible = true
		_back.focus_mode = Control.FOCUS_ALL
		if is_inside_tree() and not _back.has_focus():
			_back.grab_focus()
		return

	var built := _total > 0 and _done >= _total
	_status.text = STATUS_QUEUED if _phase == Phase.QUEUED else (
			STATUS_DONE if built else STATUS_BUILDING)
	_stage.text = ("%s · %s" % [
		"queued" if _phase == Phase.QUEUED else ("built" if built else "building"),
		_current_step(),
	]).to_upper()
	_counter.text = ("%d of %d" % [_done, _total]).to_upper()
	_ledger.text = _ledger_text()


## The step whose name belongs next to the count: the last one that reported,
## because it is the last thing that finished and nothing is in flight — the build
## is blocked, so "the step it is on" is a question with no honest answer.
func _current_step() -> String:
	if _done <= 0 or _done > _steps.size():
		return "nothing yet"
	var text := _steps[_done - 1]
	return text if not text.is_empty() else "step %d" % _done


## The eleven names, with the reported ones marked. `>` for done and `·` for
## pending, because a step list that only shows the current name hides the fact
## that all eleven arrive at once.
func _ledger_text() -> String:
	if _steps.is_empty():
		return "the step list is unavailable — circuit.gd did not load"
	var parts := PackedStringArray()
	for index: int in _steps.size():
		var text := _steps[index]
		if text.is_empty():
			text = "step %d" % (index + 1)
		parts.append(("> " if index < _done else "· ") + text)
	if _elapsed_ms > 0.0:
		parts.append("%.0f ms" % _elapsed_ms)
	return "  ".join(parts)


## Circuit name, layout and class, from whatever is actually known.
##
## Cached, and the cache is the point rather than a micro-optimization: `_refresh()`
## runs once per `step_done`, all eleven of which arrive inside one blocked build,
## and `_track_name()` parses a 45.9 KB file. Eleven parses of it would be work
## added to the exact interval this screen exists to shorten the feel of.
##
## Indexed with `[]` where the key is a contract and `Cmdline.as_string` where it
## is a genuinely optional argument. `Dictionary.get(key, default)` on a contract
## key is the trap that left four wheels dead straight through every corner.
func _target() -> Dictionary:
	if not _target_cache.is_empty():
		return _target_cache
	var track := Cmdline.as_string(_request, "track", "")
	if track.is_empty():
		var args: Dictionary = shell.args() if shell != null else {}
		track = Cmdline.as_string(args, "track", DEFAULT_TRACK)
	_target_cache = {
		"name": _track_name(track),
		"layout": Cmdline.as_string(_request, "layout", "forward"),
		# `circuit.gd:708` sets `CLASS_KZ2` unconditionally and `SessionRunner`
		# refuses anything else, so this is a fact and not a default.
		"kart_class": KartSession.kart_class_name(KartSession.CLASS_KZ2),
	}
	return _target_cache


## The circuit's display name out of `track.json`'s `meta.name`.
##
## Parsed rather than loaded through `KartTrack`: `load()` runs twenty validation
## rules and measured 2.95-3.59 ms on Valdirone, which is real work to do twice for
## a caption. `TRACK_SCHEMA.md` makes `meta.name` the display name; a file that does
## not parse falls back to its own stem, which is still true.
func _track_name(path: String) -> String:
	var stem := path.get_file().get_basename().get_basename()
	if not FileAccess.file_exists(path):
		return stem
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed is Dictionary and (parsed as Dictionary).get("meta") is Dictionary:
		var meta: Dictionary = (parsed as Dictionary)["meta"]
		if meta.has("name"):
			return String(meta["name"])
	return stem


## One tip, chosen by the day rather than at random, so a still shot from a
## recorded command is reproducible. Empty when the corpus is empty, and the row is
## then not built at all — ADR-0052's "dropped if the pool is thin", as code.
func _tip() -> String:
	if TIPS.is_empty():
		return ""
	var day := int(Time.get_unix_time_from_system() / 86400.0)
	return String(TIPS[day % TIPS.size()])


## Out of the refusal. Back to setup if the shell has one, and to the paddock if it
## does not — never nowhere.
func _leave() -> void:
	if shell == null:
		return
	if bool(shell.has_screen("setup")):
		shell.reset_to("setup")
		return
	shell.reset_to("paddock")


# --- ShellScreen ------------------------------------------------------------------


## Backing out of a build is refused; backing out of the other two states is not.
##
## Popping mid-build would reveal the screen underneath while `circuit.gd` is
## still blocked inside `_ready()`, which is a menu the player can operate over a
## session that is halfway built. `QUEUED` is the screen reached from `--screen=`
## or from the gate, where there is nothing to interrupt, and `REFUSED` is a dead
## end that must have a way out of it.
func can_pop() -> bool:
	return _phase != Phase.BUILDING


func initial_focus() -> Control:
	return _back if _back != null and _back.visible else null


func title() -> String:
	return "loading"
