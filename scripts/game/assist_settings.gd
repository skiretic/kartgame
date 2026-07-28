class_name AssistSettings
extends RefCounted

## The assist state a driver actually chose, remembered between runs.
##
## ## Why this file exists
##
## `src/core/settings.h` has carried `auto_clutch` and `auto_shift` since M3c,
## `KartSettings` binds them to GDScript, `settings.cfg` writes them as
## `assist_auto_clutch` and `assist_auto_shift`, and `profile_probe.gd` proves the
## round-trip byte for byte. **Nothing loaded any of it.** `grep` for settings in
## the two driveable scenes returned one hit each and both were
## `ProjectSettings.get_setting("physics/3d/physics_engine")`.
##
## So every launch started at `drivetrain.h`'s default — auto-shift on — and the
## G key that turns it off worked perfectly for exactly as long as the process
## lived. The report that found it was "it seemed to be in automatic shifting
## again", and *again* is the whole bug: the setting was saveable, the toggle was
## real, and the two had never been introduced.
##
## That is the same family as the three `control_hints.gd` lists in its own
## header — `look_back` bound and read by nothing, `auto_shift` with no way to
## turn it off, a pad with no on-screen controls. A capability that exists at both
## ends and is not joined in the middle is indistinguishable, from the driver's
## seat, from a capability that was never built.
##
## ## One owner, for the reason `ControlHints` gives
##
## `proving_ground.gd` and `test_track.gd` keep their own `_build_kart` on
## purpose, and that stays. This is not that. This is one decision — where the
## assist state comes from and where it goes — and two hand-maintained copies of a
## decision is how the control list drifted for a milestone.
##
## ## The rule that keeps a stored preference out of a measurement
##
## A saved file that silently changes a number a gate asserts is worse than no
## persistence at all. `shoot.sh` drives a scene from `--throttle`/`--steer`/
## `--brake` and CLAUDE.md makes it a rule that "every still is reproducible from
## the command that made it"; a `settings.cfg` sitting in `user://` with
## auto-shift off would move the kart in a still whose command says nothing about
## assists. ADR-0037 makes the same argument for tuning presets and answers it
## with an audit trail.
##
## So the stored file is consulted **only when the run is a person driving**:
##
##   * an explicit `--auto-shift` or `--auto-clutch` wins outright, because a
##     command that names an assist is not asking for a preference
##   * scripted input — any of `--throttle`, `--steer`, `--brake` — skips the file
##     entirely, which is exactly the `shoot.sh` case and every reproducible still
##
## Both skips are reported rather than silent, so a run that ignored the file says
## which rule ignored it.

## The keys a command line uses to override a stored assist.
const ARG_AUTO_SHIFT := "auto-shift"
const ARG_AUTO_CLUTCH := "auto-clutch"

## The arguments that mean "this run is scripted, not driven".
const SCRIPTED_INPUT_ARGS: PackedStringArray = ["throttle", "steer", "brake"]


## Resolve the kart's assists from the stored settings and the command line.
##
## Call it at the end of `_build_kart`, before the session copies the flags off
## the kart — the session is downstream of this by design, so `--auto-shift=off`
## is *recorded* in the configuration rather than contradicted by it, and a
## remembered preference is recorded the same way.
##
## Returns a report for the scene to print: `{loaded, existed, source, path,
## auto_shift, auto_clutch, warnings}`. `source` is one of `"default"`,
## `"settings.cfg"`, `"command line"` or `"scripted input"`.
static func apply(kart: KartBody, args: Dictionary) -> Dictionary:
	var report := {
		"loaded": false,
		"existed": false,
		"source": "default",
		"path": "",
		"warnings": PackedStringArray(),
	}
	if kart == null:
		return _finish(report, kart)

	var named_assist := args.has(ARG_AUTO_SHIFT) or args.has(ARG_AUTO_CLUTCH)
	var scripted := false
	for key in SCRIPTED_INPUT_ARGS:
		if args.has(key):
			scripted = true
			break

	# Scripted first, because it is the stronger statement: a still driven from
	# arguments must not depend on a file the command does not mention, even if
	# that same command also names an assist.
	if scripted:
		report["source"] = "scripted input"
	elif named_assist:
		report["source"] = "command line"
	elif not ClassDB.class_exists("KartSettings"):
		# A stale or missing extension build. `test_track.gd` already refuses to run
		# without `KartBody`, so this can only be reached by a partial build; say so
		# rather than silently starting at the defaults.
		report["warnings"].append("KartSettings is not registered; assists at their defaults")
	else:
		var settings := KartSettings.new()
		var loaded: Dictionary = settings.load()
		report["loaded"] = true
		report["existed"] = bool(loaded.get("existed", false))
		report["path"] = String(loaded.get("path", ""))
		report["warnings"] = loaded.get("warnings", PackedStringArray())
		# `KartSettings.load()` resets to the defaults before it reads, and reports
		# `ok` true on a missing file, so the getters are answerable either way and
		# a first run lands on the same values `drivetrain.h` would have given.
		kart.auto_clutch = settings.is_auto_clutch()
		kart.auto_shift = settings.is_auto_shift()
		if report["existed"]:
			report["source"] = "settings.cfg"

	# The command line last, so an explicit flag beats a remembered preference.
	# Defaulting to the kart's current value rather than to a literal keeps this
	# from becoming a second owner of what "on" means — the same argument the line
	# it replaced in each scene already made.
	kart.auto_shift = Cmdline.as_bool(args, ARG_AUTO_SHIFT, kart.auto_shift)
	kart.auto_clutch = Cmdline.as_bool(args, ARG_AUTO_CLUTCH, kart.auto_clutch)
	return _finish(report, kart)


## Write the kart's current assist state back to `user://settings.cfg`.
##
## Called from the toggle, not from `_exit_tree`: a preference that only survives
## a clean shutdown is a preference that does not survive the way people actually
## close a game. The write is `KartSettings.save()`, which goes through the same
## temp-then-rename-then-fsync path as the career save (ADR-0042, and #173's shim
## for the sync the engine cannot do itself), so a toggle cannot leave a truncated
## file behind.
##
## Every other key in the file is preserved: `load()` fills the whole struct from
## disk first, and only the two assists are overwritten. A camera mode or a volume
## trim set by some future menu is not lost by pressing G.
##
## Returns `{ok, path, warnings}`.
static func remember(kart: KartBody) -> Dictionary:
	var result := {"ok": false, "path": "", "warnings": PackedStringArray()}
	if kart == null or not ClassDB.class_exists("KartSettings"):
		return result
	var settings := KartSettings.new()
	var loaded: Dictionary = settings.load()
	result["warnings"] = loaded.get("warnings", PackedStringArray())
	settings.set_auto_shift(kart.auto_shift)
	settings.set_auto_clutch(kart.auto_clutch)
	var saved: Dictionary = settings.save()
	result["ok"] = bool(saved.get("ok", false))
	result["path"] = String(saved.get("path", ""))
	var warnings: PackedStringArray = result["warnings"]
	for warning in saved.get("warnings", PackedStringArray()):
		warnings.append(warning)
	result["warnings"] = warnings
	return result


## One line for the boot log, so a run that ignored the file says which rule
## ignored it and a run that read one says where it read.
static func describe(report: Dictionary) -> String:
	var text := "  assists    clutch %s shift %s, from %s" % [
		"on" if report.get("auto_clutch", true) else "off",
		"on" if report.get("auto_shift", true) else "off",
		report.get("source", "default"),
	]
	if report.get("loaded", false) and not report.get("existed", false):
		text += " (no settings.cfg yet)"
	for warning in report.get("warnings", PackedStringArray()):
		text += "\n             %s" % warning
	return text


static func _finish(report: Dictionary, kart: KartBody) -> Dictionary:
	if kart != null:
		report["auto_shift"] = kart.auto_shift
		report["auto_clutch"] = kart.auto_clutch
	return report
