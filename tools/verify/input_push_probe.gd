extends SceneTree

## Does pressing a control actually reach the solver? ADR-0040's reader check.
##
## `KartBody` no longer reads the `Input` singleton, so the chain from a binding to
## a force now has a node in the middle of it. Every gate this project owns drives
## through `KartBody.input_driver`, which is the branch that **bypasses**
## `PlayerDriver` entirely — so the whole of the player's input path could be dead
## and `drive.sh` would report four perfect figures. That is the same shape as the
## four control failures #169 exists for: a binding that is advertised and unread.
##
## So this presses the real actions through `Input.action_press`, on the real
## proving ground, and asserts the kart moves and the body reports what was pressed.
##
##     godot --headless --path . --script tools/verify/input_push_probe.gd
##       --break     disable the driver, so the probe must FAIL. Its own negative
##                   control: a check that cannot fail is not a check, and this one
##                   asserts against a kart that would coast either way.
##
## Issue #169's own gate is wider than this — it asserts the on-screen control list
## against the InputMap — and is still open. This is the driving half of it.

const SCENE_PATH := "res://scenes/game/proving_ground.tscn"

## Long enough for the spawn drop to settle, which `drive_probe.gd` also waits out.
const SETTLE_TICKS := 120

## Long enough at full throttle to be unmistakable rather than marginal.
const DRIVE_TICKS := 240

var _root: Node
var _kart: KartBody
var _driver: PlayerDriver
var _tick := 0
var _attached := false
var _failed := false
var _pressed := false
var _peak_speed := 0.0
var _peak_throttle := 0.0
var _peak_steer := 0.0

## `--break`: turn the driver off and expect every check to fail. Exit code is
## inverted under it, so the harness can assert the failure rather than read it.
var _sabotage := false


func _initialize() -> void:
	_sabotage = "--break" in OS.get_cmdline_user_args() or "--break" in OS.get_cmdline_args()
	if not ClassDB.class_exists("PlayerDriver"):
		printerr("PlayerDriver is not registered — build the extension: scons target=editor")
		_failed = true
		return
	var packed: PackedScene = load(SCENE_PATH)
	if packed == null:
		printerr("could not load " + SCENE_PATH)
		_failed = true
		return
	_root = packed.instantiate()
	get_root().add_child(_root)


func _physics_process(_delta: float) -> bool:
	if _failed:
		quit(1)
		return true
	# The scene's children do not exist until the first tick — a `--script` main
	# loop parents its scene before the tree runs, so `_ready` has not happened.
	# `shoot.gd` and `drive_probe.gd` both look their nodes up here for this reason.
	if not _attached:
		_kart = _root.find_child("Kart", false, false) as KartBody
		_driver = _root.find_child("Driver", false, false) as PlayerDriver
		if _kart == null or _driver == null:
			printerr("scene has no Kart and Driver pair — %s, %s" % [_kart, _driver])
			quit(1)
			return true
		_attached = true
		if _sabotage:
			_driver.enabled = false

	_tick += 1
	if _tick == SETTLE_TICKS:
		# The real actions, by the names `project.godot` binds. A wrong name here
		# fails as "the kart did not move", which is why the assertion below prints
		# what the body saw rather than only whether it moved.
		Input.action_press("throttle", 1.0)
		Input.action_press("steer_left", 1.0)
		_pressed = true
	if _pressed:
		_peak_speed = maxf(_peak_speed, _kart.linear_velocity.length())
		_peak_throttle = maxf(_peak_throttle, _kart.get_throttle_input())
		_peak_steer = maxf(_peak_steer, absf(_kart.get_steer_input()))
	if _tick < SETTLE_TICKS + DRIVE_TICKS:
		return false

	Input.action_release("throttle")
	Input.action_release("steer_left")
	_report()
	return true


func _report() -> void:
	var stale: int = _kart.get_stale_input_ticks()
	# 1.0 of throttle, because the driver passes the axis through untouched. The
	# steer figure is the curve's output at full stick, which is 1.0 as well —
	# `x^gamma` is the identity at the ends and only moves the middle.
	var checks := [
		["throttle reached the solver", _peak_throttle > 0.99, "%.3f" % _peak_throttle],
		["steer reached the solver", _peak_steer > 0.99, "%.3f" % _peak_steer],
		["the kart accelerated", _peak_speed > 5.0, "%.2f m/s" % _peak_speed],
		["no tick ran on stale input", stale == 0, "%d stale" % stale],
	]
	var failures := 0
	for check in checks:
		var ok: bool = check[1]
		failures += 0 if ok else 1
		print("check %-32s %s   %s" % [check[0], "PASS" if ok else "FAIL", check[2]])
	print("input push %s" % ("passed" if failures == 0 else "FAILED"))
	if _sabotage:
		# Inverted, and **not** "everything fails". A disabled driver keeps pushing
		# neutral rather than going silent — `player_driver.h` says why — so the three
		# input checks must fail and the stale check must still pass. That is a
		# stronger assertion than a blanket failure: it is the only place anything
		# proves the disabled driver still stamps its tick, which is what keeps a
		# parked kart out of the warning log.
		var stale_ok: bool = checks[3][1]
		print("negative control: %d of 3 input checks failed with the driver disabled, "
			% failures + "freshness %s" % ("held" if stale_ok else "LOST"))
		quit(0 if failures == 3 and stale_ok else 1)
	if failures > 0:
		printerr("a pressed control did not reach the solver. ADR-0040's chain is broken "
			+ "between the InputMap, PlayerDriver and KartBody.")
	quit(0 if failures == 0 else 1)
