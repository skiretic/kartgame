extends SceneTree

## The tuning audit and the tuning gate — issue #159, `src/core/tuning.h`.
##
##     tools/verify/tuning.sh
##     godot --headless --path . --script tools/verify/tuning_probe.gd -- --check
##
## Two jobs, and only the second one is a gate.
##
##   * **The audit.** "What has been tuned away from its sourced default, and by
##     how much" — for a preset, or for the defaults themselves, in which case
##     the answer is "nothing" and the run is proving the machine agrees with
##     itself. `ARCHITECTURE.md` §19 names unbounded vehicle tuning as the live
##     risk and this is the command that makes it bounded: the number of moved
##     **defended** defaults is printed first, because a session that turned four
##     guesses is within the rules and a session that moved one sourced constant
##     is a different kind of event.
##   * **The gate**, `--check`. Fifteen properties the preset format has to hold
##     for the audit to mean anything. A format that reorders itself between
##     saves is not diffable; a loader that half-applies a broken file leaves a
##     kart tuned to a configuration that exists in no file. Each check below
##     says which property it is defending and why that property matters, not
##     merely what it compares.
##
## ## No kart, on purpose
##
## `tuning_registry.h` says the audit and the file format work with an empty
## vehicle path, and that is what makes this command cheap enough to run before
## every commit: no scene, no ground plane, no physics ticks, no import of
## `assets/generated/kart.glb`. Everything below happens inside `_initialize`
## and the process quits without ever running a frame. Check 15 is the one that
## holds the engine to that promise.
##
## ## Nothing here is a second owner of the table
##
## Which tunables exist, what they range over and where their defaults came from
## all live in `src/core/tuning.h`. This file names **no key and no number** — it
## picks the tunables it moves out of the served descriptor table by their
## provenance, and moves them in `step` units. A constant appended to the table
## is therefore covered by these checks on the day it lands, and a renamed one
## does not need an edit here. The one deliberate exception is the provenance
## name table in check 2, and its comment says why it has to be a second copy.
##
## The ADR-0018 double import does not apply to a `--script` run that touches no
## asset, but `tools/verify/tuning.sh` does it anyway for the same reason every
## other wrapper does: a cold `.godot/` is seeded by the first import and this
## script's own `.uid` is written by it.

## Scratch for the round-trip files. Under `user://` because a verification tool
## that writes into the repository is one `git add -A` away from committing its
## own temporary files — CLAUDE.md records that happening twice.
const SCRATCH_DIR := "user://tuning_probe"

## Two values agree when they agree to the format's own grain. `tuning.h`
## quantizes every stored value to 1e-6 and writes exactly six decimals, so half
## a quantum is the widest tolerance that can still tell two distinct storable
## values apart, and any comparison looser than this would pass on a value that
## the file cannot represent.
const HALF_QUANTUM := 5e-7

## How far the sample set moves a tunable, in `step` units rather than in a
## magnitude. Three steps is inside every declared range from every declared
## default — `step` is sized in `tuning.h` so 20 to 40 presses cross the
## interesting part of the range — so one number moves `frame_torsion` by
## 30 N m/deg and `noise_gain` by 0.03 without either leaving its bounds.
const SAMPLE_STEPS := 3

var _lines: Array[String] = []
var _passed := 0
var _failed := 0


func _initialize() -> void:
	var args := Cmdline.parse()

	# Belt and braces, and honest about which half it is: if the extension is
	# missing entirely, this script fails to *parse* — `KartTuning` is a static
	# type below — and the shell's "no conclusion line" check is what catches
	# that. What this guard catches is the other case, an extension that loaded
	# but was built before `KartTuning` was registered.
	if not ClassDB.class_exists("KartTuning"):
		printerr("KartTuning is not registered — build the extension: scons target=editor arch=arm64")
		quit(1)
		return

	if Cmdline.as_bool(args, "check", false):
		_check()
	else:
		_audit(Cmdline.as_string(args, "preset", ""))

	print("\n".join(_lines))
	quit(1 if _failed > 0 else 0)


# --- the audit ------------------------------------------------------------------


func _audit(preset_path: String) -> void:
	var tuning := KartTuning.new()

	_lines.append("=== tuning audit ======================================================")
	_lines.append("")

	if preset_path.is_empty():
		_lines.append("    preset                       (none — auditing the defaults)")
	else:
		_lines.append("    preset                       %s" % preset_path)
		var report := tuning.load_preset(preset_path)
		if not bool(report.get("ok", false)):
			# A preset that did not load is not "an audit of the defaults", and
			# printing a clean all-defaults report for a file that failed to open
			# is the exact shape of the `SKIP_IMPORT=1` trap: a true-looking
			# report about the wrong thing.
			_failed += 1
			_lines.append("")
			_lines.append("    LOAD FAILED, error %d — nothing below describes this preset" % (
				int(report.get("error", FAILED))
			))
			for warning in report.get("warnings", PackedStringArray()):
				_lines.append("      %s" % warning)
			tuning.free()
			return
		_lines.append("    preset name                  %s" % String(report.get("name", "")))
		_lines.append("    entries applied              %d   (%d overriding a defended default)" % [
			int(report.get("applied", 0)), int(report.get("defended", 0)),
		])
		var warnings: PackedStringArray = report.get("warnings", PackedStringArray())
		if warnings.size() > 0:
			_lines.append("")
			_lines.append("    warnings, %d:" % warnings.size())
			for warning in warnings:
				_lines.append("      %s" % warning)

	_lines.append("")
	_summary(tuning)
	_lines.append("")
	_table(tuning)
	_lines.append("")
	# The same body the saved file would carry, formatted C++-side. Printed
	# beneath the table rather than instead of it, because the two are rendered
	# by different code from the same set: if they ever disagree, one of them is
	# lying about what is running and this is where it shows.
	_lines.append("--- the preset body, as `save_preset` would write it -------------------")
	_lines.append("")
	for line in tuning.audit_text().split("\n"):
		_lines.append("    %s" % line)

	_machine(tuning)
	tuning.free()


## The summary, and the ordering is the point of it.
##
## §19 is about the second number, not the first, so it goes first whenever it is
## not zero and is stated as a count of citations overridden rather than as a
## count of values. A run that moved three guesses and a run that moved one
## published measurement are not the same event and a single "3 changed" line
## reads them as the same event.
func _summary(tuning: KartTuning) -> void:
	var overrides := tuning.defended_override_count()
	if overrides > 0:
		_lines.append("    ** %d DEFENDED DEFAULT%s OVERRIDDEN — see the OVERRIDE lines below **" % [
			overrides, "" if overrides == 1 else "S",
		])
	_lines.append("    changed                      %d of %d tunables" % [
		tuning.changed_count(), tuning.tunable_count(),
	])
	_lines.append("    defended overrides           %d" % overrides)
	_lines.append("    at defaults                  %s" % (
		"yes" if tuning.is_at_defaults() else "NO"
	))
	_lines.append("    tuning hash                  %s" % tuning.tuning_hash_hex())
	_lines.append("    default hash                 %s" % tuning.default_hash_hex())


## One line per changed tunable. Unchanged ones are not printed, because a preset
## is a diff and an audit of a diff that lists the fourteen numbers nobody moved
## is a list somebody has to read to find the three that matter.
func _table(tuning: KartTuning) -> void:
	if tuning.changed_count() == 0:
		_lines.append("    Nothing has been tuned. Every value is the default its citation")
		_lines.append("    argues for, and the two hashes above are equal.")
		return

	_lines.append("    %-2s %-22s %12s %12s %12s   %s" % [
		"", "key", "value", "default", "delta", "provenance",
	])
	for id in tuning.tunable_count():
		if tuning.is_default(id):
			continue
		var d := tuning.descriptor(id)
		_lines.append("    %-2s %-22s %12.6f %12.6f %+12.6f   %s" % [
			"!" if bool(d["defended"]) else "", String(d["key"]),
			tuning.get_value(id), float(d["default_value"]), tuning.delta(id),
			String(d["provenance_name"]),
		])
		# The citation only for the defended ones, and on its own line. It is the
		# thing being overridden, so an audit that printed the value without it
		# would answer "by how much" and not "away from what".
		if bool(d["defended"]):
			_lines.append("       OVERRIDE: %s" % String(d["citation"]))
			_lines.append("       the number lives in %s" % String(d["home"]))


## Machine-readable tail, in `drive_probe.gd`'s shape: one fact per line, at
## column zero, printed last so a shell can grep for it without parsing the
## report above it.
func _machine(tuning: KartTuning) -> void:
	_lines.append("")
	_lines.append("tuning-changed %d" % tuning.changed_count())
	_lines.append("tuning-defended-overrides %d" % tuning.defended_override_count())
	_lines.append("tuning-hash %s" % tuning.tuning_hash_hex())
	_lines.append("default-hash %s" % tuning.default_hash_hex())


# --- the gate -------------------------------------------------------------------


func _check() -> void:
	_lines.append("=== tuning gate =======================================================")
	_lines.append("")
	_lines.append("    Fifteen properties, each one something the audit above would report")
	_lines.append("    wrongly rather than loudly if it did not hold. No kart, no scene and")
	_lines.append("    no physics tick — see the header.")
	_lines.append("")

	_clean_scratch()

	_check_fresh_is_default()
	_check_descriptor_table()
	_check_id_of()
	_check_defended_refuses()
	_check_reset_value_never_refused()
	_check_reset_all_drops_acknowledgements()
	_check_round_trip()
	_check_byte_identical()
	_check_defaults_write_nothing()
	_check_marker_is_first()
	_check_malformed_applies_nothing()
	_check_unknown_key_warns()
	_check_stale_defaults_hash_warns()
	_check_clamped_value_is_the_truth()
	_check_ready_with_no_vehicle()
	_check_save_never_opens_the_target()

	_clean_scratch()

	_lines.append("")
	_lines.append("checks %d passed %d failed" % [_passed, _failed])


## 1. A freshly-constructed registry is at its defaults, and its hash is the
## default hash.
##
## This is the check the whole gate rests on rather than a formality. Every §6.4
## measurement is allowed to be recorded only because `is_at_defaults()` said so
## — `tuning_registry.h` names `drive_probe.gd` as the caller — so a registry
## that came up anywhere other than its defaults would make that assertion either
## fire on every clean run or, far worse, be satisfiable by a set that is not the
## defaults at all.
func _check_fresh_is_default() -> void:
	var tuning := KartTuning.new()
	var name := "fresh registry is at its defaults"
	var at_defaults := tuning.is_at_defaults()
	var hashes_agree := tuning.tuning_hash_hex() == tuning.default_hash_hex()
	var counts_zero := tuning.changed_count() == 0 and tuning.defended_override_count() == 0
	var per_tunable_default := true
	for id in tuning.tunable_count():
		if not tuning.is_default(id) or absf(tuning.delta(id)) > HALF_QUANTUM:
			per_tunable_default = false
	_ok(name, at_defaults and hashes_agree and counts_zero and per_tunable_default,
		"%s, %d of %d changed" % [
			tuning.tuning_hash_hex(), tuning.changed_count(), tuning.tunable_count(),
		])
	tuning.free()


## 2. The descriptor bridge serves a complete, self-consistent row for every
## tunable.
##
## `core/tuning.h`'s own tests hold the C++ table. What they cannot see is the
## Dictionary this node builds out of it, which is the only thing the overlay and
## this audit ever read. A bridge that dropped `citation` on the Unsourced rows,
## or served `defended` from a stale copy of the rule, would leave every other
## check here passing and the audit quietly unable to say what it was overriding.
func _check_descriptor_table() -> void:
	var tuning := KartTuning.new()
	var required := [
		"id", "key", "label", "unit", "home", "citation",
		"default_value", "min_value", "max_value", "step",
		"provenance", "provenance_name", "defended", "owner", "owner_name",
	]
	# Deliberately a second copy of the names. The property being checked is that
	# the served int and the served string agree — `tuning_registry.h` serves both
	# because the overlay colors a row by the first and prints the second — and a
	# check that read the name from the same bridge could not fail.
	var provenance_names := ["sourced", "measured", "derived", "unsourced"]
	var problems: Array[String] = []
	var seen := {}

	for id in tuning.tunable_count():
		var d := tuning.descriptor(id)
		# A missing key skips only *this* row's remaining assertions, not the rest
		# of the table: every one of them would fault reading it, and a report that
		# stops at the first incomplete descriptor cannot say whether one row is
		# wrong or all fourteen are.
		var complete := true
		for key in required:
			if not d.has(key):
				problems.append("%d: missing %s" % [id, key])
				complete = false
		if not complete:
			continue
		var label := String(d["key"])
		if label.is_empty() or seen.has(label):
			problems.append("%d: empty or duplicate key '%s'" % [id, label])
		seen[label] = true
		if int(d["id"]) != id:
			problems.append("%s: descriptor(%d) reports id %d" % [label, id, int(d["id"])])
		var provenance := int(d["provenance"])
		if provenance < 0 or provenance >= provenance_names.size():
			problems.append("%s: provenance %d is outside the four classes" % [label, provenance])
		elif String(d["provenance_name"]) != provenance_names[provenance]:
			problems.append("%s: provenance %d served as '%s'" % [
				label, provenance, String(d["provenance_name"]),
			])
		# `is_defended` is exactly Sourced or Measured. Restated here because the
		# rule is the one §19 rests on and a bridge is free to get it wrong.
		if bool(d["defended"]) != (provenance <= 1):
			problems.append("%s: defended=%s at provenance %d" % [
				label, bool(d["defended"]), provenance,
			])
		if bool(d["defended"]) != tuning.is_defended(id):
			problems.append("%s: descriptor and is_defended() disagree" % label)
		if float(d["default_value"]) < float(d["min_value"]) \
				or float(d["default_value"]) > float(d["max_value"]):
			problems.append("%s: default is outside its own range" % label)
		if float(d["step"]) <= 0.0:
			problems.append("%s: step is %f" % [label, float(d["step"])])
		# A citation is not decoration: an Unsourced default's citation is the only
		# guidance that exists for what to listen or feel for.
		if String(d["citation"]).strip_edges().is_empty():
			problems.append("%s: empty citation" % label)
		if String(d["home"]).strip_edges().is_empty():
			problems.append("%s: empty home" % label)
		if String(d["owner_name"]).strip_edges().is_empty():
			problems.append("%s: empty owner_name" % label)

	_ok("descriptor table is complete", problems.is_empty(),
		"%d tunables" % tuning.tunable_count() if problems.is_empty() else "; ".join(problems))
	tuning.free()


## 3. `id_of` round-trips every declared key and returns -1 for one that does not
## exist.
##
## The loader's unknown-key warning is built on this returning -1 rather than 0.
## A lookup that fell back to the first tunable would turn a renamed constant in
## an old preset into a silent change to `peak_friction`, which is a tuned kart
## nobody wrote down — the same failure the malformed-line rule exists to prevent,
## arriving through a door the parser never sees.
func _check_id_of() -> void:
	var tuning := KartTuning.new()
	var problems: Array[String] = []
	for id in tuning.tunable_count():
		var key := String(tuning.descriptor(id)["key"])
		if tuning.id_of(key) != id:
			problems.append("%s -> %d, expected %d" % [key, tuning.id_of(key), id])
	# A key that cannot be in the table: the format's keys are lower-case words.
	if tuning.id_of("no_such_tunable") != -1:
		problems.append("an unknown key resolved to %d" % tuning.id_of("no_such_tunable"))
	if tuning.id_of("") != -1:
		problems.append("the empty key resolved to %d" % tuning.id_of(""))
	_ok("id_of round-trips, and rejects", problems.is_empty(), "; ".join(problems))
	tuning.free()


## 4. A defended default refuses to move until it is acknowledged, and the refusal
## returns the unchanged value.
##
## This is the whole system. If `set_value` silently accepted a defended change,
## every other check here would still pass and the audit would still print a
## correct-looking OVERRIDE line — the file would be honest about a ceremony that
## never happened. The refusal must also *return the old value*, because
## `tuning_registry.h` says a caller displays what it set: a UI that showed the
## refused value would tell the driver the constant moved when it did not.
func _check_defended_refuses() -> void:
	var tuning := KartTuning.new()
	var id := _first_defended(tuning)
	if id < 0:
		_ok("defended default refuses to move", false, "no defended tunable in the table")
		tuning.free()
		return

	var d := tuning.descriptor(id)
	var before := tuning.get_value(id)
	var target := before + float(d["step"]) * float(SAMPLE_STEPS)
	var returned := tuning.set_value(id, target)
	var refused := absf(returned - before) <= HALF_QUANTUM \
			and absf(tuning.get_value(id) - before) <= HALF_QUANTUM \
			and tuning.is_default(id) \
			and not tuning.is_acknowledged(id)

	# `nudge` is `set_value` on top of a step and must refuse identically. It is a
	# separate entry point and the overlay's d-pad reaches it rather than
	# `set_value`, so a guard installed on one and not the other defends nothing
	# that anybody actually uses.
	var nudged := tuning.nudge(id, SAMPLE_STEPS)
	var nudge_refused := absf(nudged - before) <= HALF_QUANTUM and tuning.is_default(id)

	tuning.acknowledge(id)
	var moved := tuning.nudge(id, SAMPLE_STEPS)
	var accepted := absf(moved - target) <= HALF_QUANTUM \
			and not tuning.is_default(id) \
			and tuning.is_acknowledged(id) \
			and tuning.defended_override_count() == 1

	_ok("defended default refuses, then moves", refused and nudge_refused and accepted,
		"%s: %.6f refused, %.6f after acknowledge" % [String(d["key"]), before, moved])
	tuning.free()


## 5. `reset_value` is never refused, acknowledged or not.
##
## Stated in `tuning_registry.h` and worth holding, because the obvious
## implementation routes reset through `set_value` and inherits its guard. Putting
## a sourced number back where the source put it is not an override; if it needed
## the ceremony, a slip on a defended row could only be undone by restarting, and
## a system that makes the safe move harder than the unsafe one gets switched off.
func _check_reset_value_never_refused() -> void:
	var tuning := KartTuning.new()
	var id := _first_defended(tuning)
	if id < 0:
		_ok("reset_value is never refused", false, "no defended tunable in the table")
		tuning.free()
		return

	var default_value := tuning.get_value(id)
	tuning.acknowledge(id)
	tuning.nudge(id, SAMPLE_STEPS)
	# Re-locked first, so the reset below happens on a tunable that `set_value`
	# would refuse. That is the case the guard gets wrong.
	tuning.withdraw_acknowledgement(id)
	var restored := tuning.reset_value(id)

	_ok("reset_value is never refused",
		absf(restored - default_value) <= HALF_QUANTUM
			and tuning.is_default(id)
			and not tuning.is_acknowledged(id)
			and tuning.is_at_defaults(),
		"%s back to %.6f while re-locked" % [String(tuning.descriptor(id)["key"]), restored])
	tuning.free()


## 6. `reset_all` drops every acknowledgement as well as every value.
##
## `tuning_registry.h` says both in one sentence and they are easy to implement
## separately. The acknowledgement is per-tunable and per-session precisely so it
## cannot become a global expert mode switched on in the first ten minutes; a
## `reset_all` that cleared the values and left the unlocks standing is that
## expert mode, arriving by accident.
func _check_reset_all_drops_acknowledgements() -> void:
	var tuning := KartTuning.new()
	var id := _first_defended(tuning)
	if id < 0:
		_ok("reset_all drops acknowledgements", false, "no defended tunable in the table")
		tuning.free()
		return

	tuning.acknowledge(id)
	tuning.nudge(id, SAMPLE_STEPS)
	tuning.reset_all()

	var before := tuning.get_value(id)
	var returned := tuning.set_value(id, before + float(tuning.descriptor(id)["step"]))
	_ok("reset_all drops acknowledgements",
		tuning.is_at_defaults()
			and tuning.tuning_hash_hex() == tuning.default_hash_hex()
			and not tuning.is_acknowledged(id)
			and absf(returned - before) <= HALF_QUANTUM,
		"re-locked, and the retry was refused")
	tuning.free()


## 7. A preset round-trips through a file, including a defended override.
##
## The audit reads a file and reports what a session did. If save and load do not
## compose to the identity, that report describes a configuration nobody ran, and
## every §6.4 figure recorded under the preset is attributed to the wrong set of
## constants. The two hashes agreeing is the strong form: `TuningSet::hash()`
## mixes the key alongside the value, so it also catches a loader that applied the
## right numbers to the wrong tunables.
##
## The save deliberately targets a directory that does not exist. That is the
## behavior `tuning_registry.h` promises and the reason it gives is a good one —
## the natural home is `user://tuning/` and a save that failed for a missing
## directory is a lost session.
func _check_round_trip() -> void:
	var source := KartTuning.new()
	var moved := _tune_sample(source)
	var path := SCRATCH_DIR + "/roundtrip.tune"

	var saved := source.save_preset(path, "probe-roundtrip")
	if saved != OK:
		_ok("a preset round-trips through a file", false, "save_preset returned %d" % saved)
		source.free()
		return
	if not FileAccess.file_exists(path):
		_ok("a preset round-trips through a file", false,
			"save_preset returned OK and wrote no file to %s" % path)
		source.free()
		return

	var loaded := KartTuning.new()
	var report := loaded.load_preset(path)

	var problems: Array[String] = []
	if not bool(report.get("ok", false)):
		problems.append("ok=false, error %d" % int(report.get("error", FAILED)))
	if String(report.get("name", "")) != "probe-roundtrip":
		problems.append("name came back as '%s'" % String(report.get("name", "")))
	if int(report.get("applied", -1)) != moved.size():
		problems.append("applied %d of %d" % [int(report.get("applied", -1)), moved.size()])
	if int(report.get("defended", -1)) != source.defended_override_count():
		problems.append("defended %d, expected %d" % [
			int(report.get("defended", -1)), source.defended_override_count(),
		])
	# A clean round-trip has nothing to warn about: same build, same defaults, no
	# unknown key and no clamp. A warning here means one of those four assumptions
	# is false and the rest of this check is measuring the wrong thing.
	var warnings: PackedStringArray = report.get("warnings", PackedStringArray())
	if warnings.size() > 0:
		problems.append("unexpected warnings: %s" % ", ".join(warnings))
	if loaded.tuning_hash_hex() != source.tuning_hash_hex():
		problems.append("hash %s vs %s" % [loaded.tuning_hash_hex(), source.tuning_hash_hex()])
	# Value by value as well as by hash, because a hash mismatch says only that
	# something differs. This says which tunable and by how much, which is the
	# difference between a failing gate and a diagnosable one.
	for id in source.tunable_count():
		if absf(loaded.get_value(id) - source.get_value(id)) > HALF_QUANTUM:
			problems.append("%s: %.6f vs %.6f" % [
				String(source.descriptor(id)["key"]), loaded.get_value(id), source.get_value(id),
			])

	_ok("a preset round-trips through a file", problems.is_empty(),
		"%d moved, %s" % [moved.size(), source.tuning_hash_hex()] if problems.is_empty()
			else "; ".join(problems))
	source.free()
	loaded.free()


## 8. Two saves of the same set are byte-identical.
##
## Diffable is the entire point of the format: a preset in a pull request is meant
## to be read as a diff against the defaults, and a review of a re-saved file that
## reordered its lines or restated its own hash differently is a review of noise.
## Checked in both forms because they can fail separately — `to_text` could be
## stable while `save_preset` stamped a timestamp or a path into the file it
## wrote, which is exactly the sort of thing that gets added later for good
## reasons and destroys the property silently.
func _check_byte_identical() -> void:
	var tuning := KartTuning.new()
	_tune_sample(tuning)

	var first_text := tuning.to_text("probe-stable")
	var second_text := tuning.to_text("probe-stable")

	var path_a := SCRATCH_DIR + "/stable_a.tune"
	var path_b := SCRATCH_DIR + "/stable_b.tune"
	var saved_a := tuning.save_preset(path_a, "probe-stable")
	var saved_b := tuning.save_preset(path_b, "probe-stable")
	var bytes_a := FileAccess.get_file_as_bytes(path_a)
	var bytes_b := FileAccess.get_file_as_bytes(path_b)

	var problems: Array[String] = []
	if first_text != second_text:
		problems.append("to_text differs between two calls")
	if saved_a != OK or saved_b != OK:
		problems.append("save_preset returned %d and %d" % [saved_a, saved_b])
	elif bytes_a != bytes_b:
		problems.append("the two files differ (%d vs %d bytes)" % [bytes_a.size(), bytes_b.size()])
	# And the file is the text, rather than the text plus whatever the writer
	# added on the way out. Without this, `to_text` could be the stable thing and
	# the artifact people actually diff could not be.
	elif first_text.to_utf8_buffer() != bytes_a:
		problems.append("save_preset wrote %d bytes for a %d-byte to_text" % [
			bytes_a.size(), first_text.to_utf8_buffer().size(),
		])

	_ok("two saves are byte-identical", problems.is_empty(),
		"%d bytes, sha256 %s" % [bytes_a.size(), FileAccess.get_sha256(path_a).left(16)]
			if problems.is_empty() else "; ".join(problems))
	tuning.free()


## 9. A preset that is all defaults writes a body with no entry lines.
##
## A preset is a diff. A file that listed all fourteen tunables at their defaults
## would load identically and would be worthless for the one job the format has:
## "the complete list of what was tuned" is only readable as a list if the untuned
## ones are absent. The header lines are asserted present in the same breath,
## because "no entry lines" is also true of an empty string and of a save that
## failed.
func _check_defaults_write_nothing() -> void:
	var tuning := KartTuning.new()
	var text := tuning.to_text("probe-stock")

	var entries := 0
	var headers := {}
	for raw in text.split("\n"):
		var line := String(raw).strip_edges()
		if line.is_empty() or line.begins_with("#"):
			continue
		if line.contains("="):
			entries += 1
			continue
		headers[line.split(" ")[0]] = true

	var problems: Array[String] = []
	if entries != 0:
		problems.append("%d entry lines for a set at its defaults" % entries)
	for required in ["format", "name", "defaults", "tuned"]:
		if not headers.has(required):
			problems.append("no '%s' header" % required)
	# And the two hashes in the header of an untouched set are the same hash,
	# which is the file-level form of check 1.
	if not text.contains(tuning.default_hash_hex()):
		problems.append("the header does not carry %s" % tuning.default_hash_hex())

	_ok("an all-default preset has no entries", problems.is_empty(),
		"%d bytes of header only" % text.length() if problems.is_empty() else "; ".join(problems))
	tuning.free()


## 10. A defended override's `!` is first on the line, and nothing else carries
## one.
##
## `tuning.h` puts the marker before the key rather than in the trailing comment
## on purpose: it has to survive a `grep '^!'`, a diff hunk that shows three lines
## of context, and a terminal narrow enough to have wrapped the comment off the
## right-hand edge. A marker placed anywhere else is invisible in all three. The
## converse half matters as much — if an undefended line also got a `!` the marker
## would stop meaning anything, and the grep would return the whole file.
func _check_marker_is_first() -> void:
	var tuning := KartTuning.new()
	var moved := _tune_sample(tuning)
	var text := tuning.to_text("probe-marker")

	var problems: Array[String] = []
	var marked := 0
	for id in moved:
		var d := tuning.descriptor(id)
		var key := String(d["key"])
		var found := ""
		for raw in text.split("\n"):
			var line := String(raw)
			if line.contains(key + " ="):
				found = line
				break
		if found.is_empty():
			problems.append("no line for %s" % key)
			continue
		if bool(d["defended"]):
			marked += 1
			# `begins_with`, not `contains`: column zero is the property.
			if not found.begins_with("! " + key + " ="):
				problems.append("%s: '%s'" % [key, found.left(40)])
			if not found.contains("OVERRIDE: "):
				problems.append("%s: no OVERRIDE on the line" % key)
			# Enough of the citation to prove it is *the* citation and not a
			# restatement. Not the whole string: the formatter is entitled to run
			# out of line buffer on a long one and failing on that would be a
			# false alarm about a real limit.
			var citation := String(d["citation"])
			if not found.contains(citation.left(mini(citation.length(), 40))):
				problems.append("%s: the citation is not the one on the line" % key)
		elif found.begins_with("!"):
			problems.append("%s is undefended and carries a marker" % key)

	if marked == 0:
		problems.append("the sample moved no defended tunable, so nothing was checked")

	_ok("a defended override's ! is first", problems.is_empty(),
		"%d marked of %d moved" % [marked, moved.size()] if problems.is_empty()
			else "; ".join(problems))
	tuning.free()


## 11. A malformed line fails the whole load and applies nothing.
##
## Stated in both headers, and it is the rule with the worst failure mode behind
## it: a preset that half-applied leaves the kart tuned to a configuration that
## exists in no file, so the audit, the saved preset and the running solver would
## each be describing something different. The malformed line goes **last**, after
## four entries that parse perfectly, because that is the only arrangement a
## half-applying loader fails — one that aborted on the first line would pass a
## test that put the bad line at the top.
func _check_malformed_applies_nothing() -> void:
	var source := KartTuning.new()
	var moved := _tune_sample(source)
	if moved.is_empty():
		_ok("a malformed line applies nothing", false, "the sample moved nothing")
		source.free()
		return
	var key := String(source.descriptor(moved[moved.size() - 1])["key"])
	# "1.0abc" is `tuning.h`'s own example of the thing that must not parse as a
	# prefix. A silently truncated "3.0abc" is a tuned kart nobody asked for.
	#
	# The explicit newline is not belt and braces: whether `to_text` ends in one
	# is not in the contract, and appending without it would fuse the bad text
	# onto the last good entry and turn this into a check that the *last line*
	# fails rather than that a *later line* does.
	var text := source.to_text("probe-malformed") + "\n" + ("%s = 1.0abc\n" % key)

	var target := KartTuning.new()
	var report := target.from_text(text)

	var problems: Array[String] = []
	if bool(report.get("ok", true)):
		problems.append("ok=true for a malformed file")
	if int(report.get("applied", -1)) != 0:
		problems.append("applied %d entries" % int(report.get("applied", -1)))
	if int(report.get("error", OK)) == OK:
		problems.append("error came back as OK")
	if not target.is_at_defaults():
		problems.append("%d tunables were moved anyway" % target.changed_count())
	if target.tuning_hash_hex() != target.default_hash_hex():
		problems.append("hash %s is not the default hash" % target.tuning_hash_hex())

	_ok("a malformed line applies nothing", problems.is_empty(),
		"4 good entries ahead of it, none applied" if problems.is_empty()
			else "; ".join(problems))
	source.free()
	target.free()


## 12. An unknown key warns, and the rest of the file still applies.
##
## The opposite of check 11, and the distinction is deliberate: a *malformed* line
## means the file cannot be trusted, while an *unknown key* means one constant was
## renamed or retired and every other line still says exactly what it meant.
## `tuning.h` says the loader reports it "rather than dropping it silently" —
## silently is the failure, because a preset that quietly stops applying a
## constant somebody tuned by feel is a session's work disappearing with no line
## on stderr.
func _check_unknown_key_warns() -> void:
	var source := KartTuning.new()
	var moved := _tune_sample(source)
	# The leading newline for the reason check 11 gives: `to_text`'s trailing
	# newline is not in the contract, and a fused line would be malformed rather
	# than unknown, which is the other check.
	var text := source.to_text("probe-unknown") + "\nretired_tunable = 1.000000\n"

	var target := KartTuning.new()
	var report := target.from_text(text)
	var warnings: PackedStringArray = report.get("warnings", PackedStringArray())

	var named := false
	for warning in warnings:
		if String(warning).contains("retired_tunable"):
			named = true

	var problems: Array[String] = []
	if not bool(report.get("ok", false)):
		problems.append("ok=false — an unknown key is not a malformed file")
	if int(report.get("applied", -1)) != moved.size():
		problems.append("applied %d of %d good entries" % [
			int(report.get("applied", -1)), moved.size(),
		])
	if not named:
		problems.append("no warning names the unknown key (%d warnings)" % warnings.size())
	if target.tuning_hash_hex() != source.tuning_hash_hex():
		problems.append("the good entries did not survive: %s vs %s" % [
			target.tuning_hash_hex(), source.tuning_hash_hex(),
		])

	_ok("an unknown key warns and applies the rest", problems.is_empty(),
		"%d warnings, %d applied" % [warnings.size(), int(report.get("applied", -1))]
			if problems.is_empty() else "; ".join(problems))
	source.free()
	target.free()


## 13. A `defaults` hash that does not match this build is a warning, not a
## refusal.
##
## `tuning_registry.h` states it and gives the reason: a preset written before
## somebody moved a default still says what it meant, because every line carries
## its own key and value — what it no longer means is the same thing *relative to
## the defaults*, and the reader has to be told which. Refusing the load instead
## would make every preset in the repository unloadable the day one default moves,
## which is the day somebody most wants to compare against them.
func _check_stale_defaults_hash_warns() -> void:
	var source := KartTuning.new()
	var moved := _tune_sample(source)

	# All-zero rather than a plausible digest: `TuningSet::hash()` returning
	# exactly zero for the real table is not something to design around, so this
	# cannot collide with a build that this preset really was written against.
	var parts := source.to_text("probe-stale").split("\n")
	var rewritten := false
	for index in parts.size():
		if String(parts[index]).begins_with("defaults "):
			parts[index] = "defaults 0x0000000000000000"
			rewritten = true

	var target := KartTuning.new()
	var report := target.from_text("\n".join(parts))
	var warnings: PackedStringArray = report.get("warnings", PackedStringArray())

	var named := false
	for warning in warnings:
		if String(warning).to_lower().contains("default"):
			named = true

	var problems: Array[String] = []
	if not rewritten:
		problems.append("no 'defaults' header to rewrite")
	if not bool(report.get("ok", false)):
		problems.append("ok=false — a stale defaults hash must not refuse the load")
	if int(report.get("applied", -1)) != moved.size():
		problems.append("applied %d of %d" % [int(report.get("applied", -1)), moved.size()])
	if not named:
		problems.append("no warning mentions the defaults (%d warnings)" % warnings.size())
	if target.tuning_hash_hex() != source.tuning_hash_hex():
		problems.append("the entries did not apply: %s vs %s" % [
			target.tuning_hash_hex(), source.tuning_hash_hex(),
		])

	_ok("a stale defaults hash only warns", problems.is_empty(),
		"%d warnings, %d applied" % [warnings.size(), int(report.get("applied", -1))]
			if problems.is_empty() else "; ".join(problems))
	source.free()
	target.free()


## 14. A value outside its range is clamped, and the file carries the clamp.
##
## `tuning.h` calls the bounds plausibility limits rather than opinions, and
## clamps instead of rejecting because the caller is a thumb on a d-pad. The half
## that has to be checked is the second one: what gets *stored* is the clamped
## value, so what gets *saved* is the clamped value, so the file is the truth. A
## registry that returned the clamp to the caller and kept the raw number would
## write a preset that loads back as something else, and the audit's "by how much"
## would be a number nothing ever ran at.
func _check_clamped_value_is_the_truth() -> void:
	var tuning := KartTuning.new()
	var high := _first_undefended(tuning)
	if high < 0:
		_ok("an out-of-range value is clamped", false, "no undefended tunable in the table")
		tuning.free()
		return

	var d := tuning.descriptor(high)
	var maximum := float(d["max_value"])
	var minimum := float(d["min_value"])
	var problems: Array[String] = []

	var above := tuning.set_value(high, maximum + 1000.0)
	if absf(above - maximum) > HALF_QUANTUM or absf(tuning.get_value(high) - maximum) > HALF_QUANTUM:
		problems.append("above max: returned %.6f, stored %.6f, max %.6f" % [
			above, tuning.get_value(high), maximum,
		])

	var path := SCRATCH_DIR + "/clamped.tune"
	tuning.save_preset(path, "probe-clamped")
	var loaded := KartTuning.new()
	loaded.load_preset(path)
	if absf(loaded.get_value(high) - maximum) > HALF_QUANTUM:
		problems.append("the file carried %.6f, not the clamp %.6f" % [
			loaded.get_value(high), maximum,
		])
	if loaded.tuning_hash_hex() != tuning.tuning_hash_hex():
		problems.append("the clamped set did not round-trip")

	tuning.set_value(high, minimum - 1000.0)
	if absf(tuning.get_value(high) - minimum) > HALF_QUANTUM:
		problems.append("below min: stored %.6f, min %.6f" % [tuning.get_value(high), minimum])

	# NaN is the one input a d-pad cannot produce and a hand-edited file or a
	# GDScript caller can. `TuningSet::set` sends it to the default rather than
	# poisoning the hash — a NaN stored here would make `tuning_micro` zero and
	# two different configurations hash alike.
	tuning.set_value(high, NAN)
	if is_nan(tuning.get_value(high)):
		problems.append("NaN was stored")

	_ok("an out-of-range value is clamped", problems.is_empty(),
		"%s clamped to [%.6f, %.6f]" % [String(d["key"]), minimum, maximum]
			if problems.is_empty() else "; ".join(problems))
	tuning.free()
	loaded.free()


## 15. `_ready` with no vehicle path changes nothing and does not crash.
##
## The empty vehicle path is what makes this whole command cheap —
## `tuning_registry.h` says so in as many words — and `_ready` calls `apply_all()`
## unconditionally. Two things could go wrong and both would be invisible in every
## check above, which construct the node bare: it could fault resolving an empty
## `NodePath`, or it could push each value out through a setter and read something
## back, so that a registry which has been in a tree is no longer at its defaults.
## The second one would disable the §6.4 gate in exactly the scenes that have a
## registry.
func _check_ready_with_no_vehicle() -> void:
	var tuning := KartTuning.new()
	tuning.name = "TuningNoVehicle"
	var before := tuning.tuning_hash_hex()
	get_root().add_child(tuning)

	var problems: Array[String] = []
	if not tuning.is_at_defaults():
		problems.append("%d tunables moved during _ready" % tuning.changed_count())
	if tuning.tuning_hash_hex() != before:
		problems.append("hash %s -> %s" % [before, tuning.tuning_hash_hex()])
	if String(tuning.get_vehicle_path()) != "":
		problems.append("vehicle_path came up as '%s'" % tuning.get_vehicle_path())

	# And again explicitly, because `_ready` calling it once does not prove a
	# second caller is safe: `apply_all()` is public and every load ends with one.
	tuning.apply_all()
	if not tuning.is_at_defaults():
		problems.append("apply_all() moved a value")

	_ok("_ready with no vehicle path is inert", problems.is_empty(), "; ".join(problems))
	get_root().remove_child(tuning)
	tuning.free()


## 16. Saving a preset never opens the target for writing.
##
## It used to, and that is three defects in five lines. `FileAccess.open(target,
## WRITE)` **truncates on open**, so between that call and the first store the
## preset on disk was zero bytes — kill the process there and the file is not
## half-written, it is empty. It also failed on a read-only target where a rename
## would have succeeded, and it ignored the return of both `store_string` and
## `close`, so a full disk reported `OK`.
##
## `GAMEDESIGN.md` §8 lists presets in what persists and §5 has them surviving a
## season re-run, so they are user data by exactly the argument ADR-0042 makes about
## the career save.
##
## **The check is a permission bit, and it works because POSIX `rename` needs write
## permission on the *directory*, not on the file.** A save that succeeds over a
## 0444 target cannot have opened it. A direct open fails there with error 12, which
## is what this used to do — so the check has a real negative, not just a green.
##
## What this does not prove: durability across a power cut. `FileAccess` exposes no
## fsync of any kind, and `src/core/profile.h` states that limit at length.
func _check_save_never_opens_the_target() -> void:
	var tuning := KartTuning.new()
	_tune_sample(tuning)

	var path := SCRATCH_DIR + "/readonly.tune"
	var problems: Array[String] = []

	var first := tuning.save_preset(path, "probe-readonly")
	if first != OK:
		problems.append("the first save returned %d" % first)
	var before := FileAccess.get_file_as_bytes(path).size()

	# Lock the file itself, leaving the directory writable.
	var locked := OS.execute("/bin/chmod", ["0444", ProjectSettings.globalize_path(path)])
	if locked != 0:
		problems.append("chmod returned %d" % locked)

	# Move a value so the second save is a different file, or "unchanged" would be
	# indistinguishable from "did nothing".
	tuning.nudge(_first_undefended(tuning), 1)
	var second := tuning.save_preset(path, "probe-readonly-2")
	var after := FileAccess.get_file_as_bytes(path)
	if second != OK:
		problems.append("saving over a 0444 target returned %d -- it opened the target" % second)
	elif after.size() == 0:
		problems.append("the target is empty after the save")
	elif after.get_string_from_utf8().find("probe-readonly-2") < 0:
		problems.append("the second save did not land")

	# And no temporary is left behind.
	if FileAccess.file_exists(path + ".tmp"):
		problems.append("a .tmp survived the save")

	_ok("saving never opens the target", problems.is_empty(),
		"%d bytes, then %d over a 0444 target, no .tmp left" % [before, after.size()]
			if problems.is_empty() else "; ".join(problems))
	tuning.free()


# --- shared -----------------------------------------------------------------------


## The set every file check is built from: the first defended tunable, moved with
## an acknowledgement, and the first three undefended ones.
##
## Picked out of the table by provenance rather than named, because naming
## `steer_gamma` here would make this file a second owner of which constants
## exist — and `tuning.h` says the table is meant to grow by appending. A tunable
## added tomorrow is covered by every check above on the day it lands.
##
## Moved in `step` units rather than by a magnitude, so one number works for a
## tunable ranging over 0.0-0.6 and one ranging over 100-3500, and neither leaves
## its bounds. Returns the ids it moved, which is what the callers count against.
func _tune_sample(tuning: KartTuning) -> Array[int]:
	var moved: Array[int] = []

	var defended := _first_defended(tuning)
	if defended >= 0:
		tuning.acknowledge(defended)
		tuning.nudge(defended, SAMPLE_STEPS)
		moved.append(defended)

	var undefended := 0
	for id in tuning.tunable_count():
		if tuning.is_defended(id) or undefended >= 3:
			continue
		tuning.nudge(id, SAMPLE_STEPS)
		moved.append(id)
		undefended += 1

	# A nudge that hit a clamp would leave a tunable at its default and every
	# count downstream short by one, which would read as a save-and-load defect
	# rather than as this helper's assumption expiring. Caught here instead.
	if tuning.changed_count() != moved.size():
		_failed += 1
		_lines.append("check %-38s FAIL   the sample moved %d tunables, %d took" % [
			"sample set is what it says", moved.size(), tuning.changed_count(),
		])
	return moved


func _first_defended(tuning: KartTuning) -> int:
	for id in tuning.tunable_count():
		if tuning.is_defended(id):
			return id
	return -1


func _first_undefended(tuning: KartTuning) -> int:
	for id in tuning.tunable_count():
		if not tuning.is_defended(id):
			return id
	return -1


## One result line. A passing check still prints its measurement rather than a
## bare PASS: `contact_probe.gd`'s discipline, and the reason is that a gate whose
## output is fifteen identical words tells a reader nothing about what changed
## between two runs of it.
func _ok(name: String, condition: bool, detail: String = "") -> bool:
	if condition:
		_passed += 1
		_lines.append("check %-38s PASS%s" % [
			name, "" if detail.is_empty() else "   " + detail,
		])
	else:
		_failed += 1
		_lines.append("check %-38s FAIL   %s" % [name, detail])
	return condition


## Scratch files, before and after. Removed rather than overwritten: check 7
## asserts that `save_preset` creates its own directory, which is only a
## measurement if the directory is genuinely absent — and a leftover file from a
## previous run passing for this run's output is the `SKIP_IMPORT=1` trap in
## another costume.
func _clean_scratch() -> void:
	var dir := DirAccess.open(SCRATCH_DIR)
	if dir != null:
		for file in dir.get_files():
			DirAccess.remove_absolute(SCRATCH_DIR + "/" + file)
	DirAccess.remove_absolute(SCRATCH_DIR)
