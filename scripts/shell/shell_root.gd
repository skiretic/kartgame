class_name ShellRoot
extends Node3D

## The shell. `project.godot`'s `run/main_scene`, and therefore the answer to the
## demo definition's first line: **the game boots into itself.**
##
##     godot --path .                          the paddock
##     godot --path . -- --backdrop=flat       no generated assets needed
##     godot --path . -- --screen=settings     straight to one screen, for a still
##
## ## What lives here and what does not
##
## ADR-0053 §1: one shell scene owning a 3D backdrop and a UI layer, with every
## menu a `Control` pushed on a stack. Starting a session **swaps** to the track
## scene rather than instantiating it as a child — that is the ADR's wording, and
## it also frees the backdrop's meshes for the duration of the drive, which the
## instantiate-as-child alternative does not.
##
## **No simulation state is built here, ever.** No `KartBody`, no `PlayerDriver`,
## no `SessionRunner`, no `KartTuning`, no `KartLapTimer`. `GAMEDESIGN.md` §9 says
## it in a sentence and `shell_probe.gd` check 9b turns that sentence into an
## assertion over the live subtree, because it is the property M6's determinism
## harness rests on: a shell that quietly held a solver would make two runs of the
## same replay differ by whatever the menu did.
##
## The one message across the boundary is `SessionRequest`, which is read-once in
## both directions and lets the command line win every key it carries. See its
## header — the merge order is why no probe and no recorded `shoot.sh` had to
## change.
##
## ## Process mode
##
## The UI layer is `PROCESS_MODE_ALWAYS` so a menu still runs under
## `get_tree().paused`. The shell itself never pauses the tree — pause lives in
## the track scene and gates input at the driver, per ADR-0052 — but
## `process_mode` is **inherited**, and a screen that stopped processing the
## moment something else paused the tree would be a menu you cannot leave.

const SCREEN_DIR := "res://scripts/shell/screens/"

## Every screen the shell knows how to open, by the name a caller uses.
##
## Existence-checked rather than `preload`ed, and that is deliberate: this file
## lands before the screens do, several of them are built in parallel worktrees,
## and a `preload` of a file that is not there yet is a parse error that takes the
## whole shell down. `open()` returning false is a screen that is not built; the
## paddock does not offer one, and the gate names it.
const SCREENS := {
	"boot": SCREEN_DIR + "boot_screen.gd",
	"paddock": SCREEN_DIR + "paddock_screen.gd",
	"setup": SCREEN_DIR + "setup_screen.gd",
	"loading": SCREEN_DIR + "loading_screen.gd",
	"results": SCREEN_DIR + "results_screen.gd",
	"settings": SCREEN_DIR + "settings_screen.gd",
	"profile": SCREEN_DIR + "profile_screen.gd",
	"pause": SCREEN_DIR + "pause_screen.gd",
}

## Where a session goes. A new three-line scene on the same `circuit.gd`, with no
## default track baked into it.
##
## **`valdirone.tscn` is deliberately not reused.** `bake.sh`'s lightmap contract
## is by node path — `circuit.gd:110` says renaming `GroundVisual` silently
## unlights the ground — and every recorded still command names that scene. The
## two collapse into one once a second circuit exists; the ticket says so.
const TRACK_SCENE := "res://scenes/game/circuit.tscn"

## Arguments injected before the node enters the tree, winning over the command
## line. The one caller is `shell_probe.gd`, which forces `--backdrop=flat`: a
## `--script` run has no user args of its own to pass, and the gate must never
## touch `assets/generated/` because a fresh worktree does not have it.
var arg_override := {}

var _args := {}
var _stack: ScreenStack
var _backdrop: ShellBackdrop
var _profile: KartProfile
var _settings: KartSettings
var _boot_notes := PackedStringArray()


func _ready() -> void:
	# The shell reads the command line directly. It never calls
	# `SessionRequest.take()` -- that is the track scene's call, and draining the
	# posted config here would mean the session started with nothing.
	_args = Cmdline.parse()
	_args.merge(arg_override, true)

	_backdrop = ShellBackdrop.new()
	add_child(_backdrop)
	_backdrop.build(_args)

	var layer := CanvasLayer.new()
	layer.name = "UI"
	layer.process_mode = Node.PROCESS_MODE_ALWAYS
	add_child(layer)

	_stack = ScreenStack.new()
	_stack.shell_root = self
	layer.add_child(_stack)

	_load_persistence()
	_report()
	_open_first_screen()


# --- services the screens share ------------------------------------------------
#
# One `KartProfile` and one `KartSettings` for the whole shell. Two screens each
# holding their own would each read a different copy off disk and the second one
# to save would overwrite the first, silently -- and this is the file that finally
# makes something in `scripts/` touch `KartProfile` at all, so it starts with one
# owner rather than acquiring one after the first lost lap time.


func args() -> Dictionary:
	return _args


func stack() -> ScreenStack:
	return _stack


func backdrop() -> ShellBackdrop:
	return _backdrop


func profile() -> KartProfile:
	return _profile


func settings() -> KartSettings:
	return _settings


## Re-read `settings.cfg` after a screen has written it, so a change made on the
## settings screen is what the next session is configured from.
func reload_settings() -> void:
	if _settings != null:
		_settings.load()


func _load_persistence() -> void:
	if not ClassDB.class_exists("KartProfile"):
		# A stale or missing extension build, and it reads exactly like a
		# registration bug. CLAUDE.md: `scons target=editor arch=arm64` -- the
		# default `template_debug` target builds the one library nothing runs.
		_boot_notes.append("KartProfile is not registered -- build the extension "
				+ "with `scons target=editor arch=arm64`")
		return

	_profile = KartProfile.new()
	var loaded: Dictionary = _profile.load()
	if bool(loaded.get("existed", false)):
		_boot_notes.append("profile   %s, %d best%s" % [
			_profile.get_driver_name(), _profile.best_count(),
			"" if _profile.best_count() == 1 else "s",
		])
	else:
		_boot_notes.append("profile   none yet at %s" % _profile.profile_path())
	for warning: String in loaded.get("warnings", PackedStringArray()):
		_boot_notes.append("          %s" % warning)

	_settings = KartSettings.new()
	var read: Dictionary = _settings.load()
	_boot_notes.append("settings  %s" % (
		_settings.settings_path() if bool(read.get("existed", false))
		else "defaults, none saved yet"
	))


# --- screens -------------------------------------------------------------------


## Is a screen built? The paddock asks before offering a row, so an unbuilt screen
## is an absent row rather than a button that does nothing.
func has_screen(key: String) -> bool:
	return SCREENS.has(key) and ResourceLoader.exists(SCREENS[key])


## Push a screen by name. Returns false, loudly, when the file is not there.
func open(key: String) -> bool:
	if not has_screen(key):
		push_warning("no screen %s (%s)" % [key, SCREENS.get(key, "unregistered")])
		return false
	var script := load(SCREENS[key]) as GDScript
	if script == null:
		push_warning("%s did not load as a script" % SCREENS[key])
		return false
	_stack.push(script.new() as ShellScreen)
	return true


## Back to one screen, discarding the stack. The way home from a session.
func reset_to(key: String) -> bool:
	if not has_screen(key):
		return false
	var script := load(SCREENS[key]) as GDScript
	if script == null:
		return false
	_stack.reset_to(script.new() as ShellScreen)
	return true


## Boot, unless there is a result waiting.
##
## Coming back from a session, `SessionRequest` is holding the classification and
## the lap ledger, so the shell opens the results sheet instead of the paddock —
## the player did not ask to go to a menu, they finished a session. `--screen=`
## overrides both, which is how one still per screen gets shot from a command.
func _open_first_screen() -> void:
	var wanted := Cmdline.as_string(_args, "screen", "")
	if not wanted.is_empty():
		if open(wanted):
			return
		push_warning("--screen=%s is not built; opening boot" % wanted)
	if SessionRequest.has_result() and has_screen("results"):
		open("results")
		return
	open("boot" if Cmdline.as_bool(_args, "boot", true) else "paddock")


# --- starting and ending a session ---------------------------------------------


## Hand off to the track scene. The setup screen's last call.
##
## `config` is `circuit.gd`'s own argument vocabulary — `track`, `layout`,
## `session`, `laps`, `ghost` — because a menu and a shell script should say the
## same thing the same way. The loading screen is pushed first so the swap has
## something behind it; `circuit.gd` builds synchronously in `_ready()`, so what
## the player actually sees is that screen's last frame for the whole build.
func start_session(config: Dictionary) -> void:
	SessionRequest.post(config)
	if has_screen("loading"):
		open("loading")
	var error := get_tree().change_scene_to_file(TRACK_SCENE)
	if error != OK:
		push_error("could not open %s (%d)" % [TRACK_SCENE, error])
		SessionRequest.clear()
		reset_to("paddock")


## The way back from a session, called by the track scene.
static func return_to_shell(tree: SceneTree) -> void:
	tree.change_scene_to_file("res://scenes/shell/shell.tscn")


# --- report ---------------------------------------------------------------------


## What the shell found, as lines, on stdout.
##
## The same reason every other scene in this project prints one: a run that fell
## back — to the placeholder font, to a backdrop with no meshes, to defaults
## because the extension is stale — must say so in its own log. A still shot
## against the wrong font is otherwise diagnosed by eye, badly.
func _report() -> void:
	var lines := PackedStringArray(["shell"])
	for line: String in _backdrop.report():
		lines.append("  %s" % line)
	lines.append("  font      %s" % ShellTheme.font_source())
	for note: String in _boot_notes:
		lines.append("  %s" % note)
	var missing := PackedStringArray()
	for key: String in SCREENS:
		if not has_screen(key):
			missing.append(key)
	if not missing.is_empty():
		lines.append("  unbuilt   %s" % ", ".join(missing))
	print("\n".join(lines))


## For the boot screen's check line and for the gate.
func boot_notes() -> PackedStringArray:
	return _boot_notes
