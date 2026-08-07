class_name SessionRequest
extends RefCounted

## The one message the shell sends the track scene, and the one it gets back.
##
## ## Why this is not an autoload
##
## ADR-0053 §1 rejected threading shell state through a singleton, and this is
## the thing that would otherwise have become one. So it is deliberately *not* a
## place to keep state: both dictionaries are **read-once**, cleared by the call
## that reads them, and that is the whole safety property. If `take()` or
## `take_result()` ever stops clearing, a stale configuration or a stale result
## survives into the next session, the shell is holding simulation state again,
## and `shell_probe.gd` check 9b — no `KartBody`, `PlayerDriver`, `SessionRunner`,
## `KartTuning` or `KartLapTimer` anywhere under `ShellRoot` — stops being the
## guarantee M6's determinism harness depends on.
##
## ## The command line still wins, and that is load-bearing
##
## `circuit.gd` was `_args = Cmdline.parse()`, and `drive.sh`, `circuit.sh`,
## `bake.sh`, `session_probe.gd` and every recorded `shoot.sh` invocation depend
## on the command line staying authoritative. So `take()` merges the posted keys
## **first** and the command-line keys **second**, and any key the command line
## carries wins outright.
##
## That single ordering choice is why not one probe or still command had to
## change: with nothing posted, `take(Cmdline.parse())` is `Cmdline.parse()`. Get
## it backwards and every recorded invocation quietly changes meaning while
## continuing to run and print plausible numbers.
##
## Values are strings, because that is what `Cmdline.parse()` produces and the
## merge has to be between two dictionaries of the same shape. `Cmdline.as_*`
## reads either one afterwards without knowing which side a key came from.

static var _config: Dictionary = {}
static var _result: Dictionary = {}


# --- going in ----------------------------------------------------------------


## The shell fills this immediately before swapping to the track scene.
##
## Keys are `circuit.gd`'s documented argument names — `track`, `layout`,
## `session`, `laps`, `camera`, `tune`, `preset`, `ghost` — because the whole
## point is that a menu and a shell script say the same thing the same way.
## `shell_probe.gd` check 11 asserts this set is a subset of what the scene
## documents, so a menu cannot invent an argument the scene has never heard of.
static func post(config: Dictionary) -> void:
	_config = {}
	for key: Variant in config:
		_config[String(key)] = String(config[key])


## The track scene's only call. Merged, then cleared.
static func take(from_cmdline: Dictionary) -> Dictionary:
	var merged := _config.duplicate()
	_config = {}
	for key: Variant in from_cmdline:
		merged[key] = from_cmdline[key]
	return merged


static func has_config() -> bool:
	return not _config.is_empty()


# --- coming back -------------------------------------------------------------


## The track scene fills this when the session ends, before swapping back.
##
## `SessionRunner.result()` plus the two things it does not carry and the results
## sheet needs: the per-lap ledger, and the circuit's display name. The runner
## deals in a track *slug*; a classification sheet has a masthead.
static func deliver(result: Dictionary) -> void:
	_result = result.duplicate(true)


## The shell drains this once, on the way back in. Cleared, so a second visit to
## the paddock does not re-open a results sheet for a session that finished ten
## minutes ago.
static func take_result() -> Dictionary:
	var drained := _result
	_result = {}
	return drained


static func has_result() -> bool:
	return not _result.is_empty()


## Both sides, for a probe that needs a clean slate between cases. Not called by
## the game — a game that needs this has a leak somewhere.
static func clear() -> void:
	_config = {}
	_result = {}
