extends SceneTree

## The save gate — ADR-0042, `src/core/profile.h`, `src/session/kart_profile.h`.
##
##     godot --headless --path . --script tools/verify/profile_probe.gd
##
## Twenty-four properties the career save and the settings file have to hold, each
## one something that would go wrong **quietly** if it did not. A save format is
## the one part of a game whose defects surface an hour after they are introduced,
## on somebody else's machine, with the evidence already destroyed — so the checks
## below are weighted toward the destructive cases rather than toward the round
## trip.
##
## Every check says which property it defends and why that property matters, in
## `tuning_probe.gd`'s style, and prints its measurement rather than a bare PASS.
## The count and a non-zero exit come at the end.
##
## ## What this proves that `tests/core/test_profile.cpp` cannot
##
## The C++ suite covers the format completely and it has no filesystem: every
## function in `profile.h` takes a caller-supplied buffer, deliberately. So the
## whole of the policy that *needs* a filesystem is untested by it —
##
##   * that `user://` resolution puts the three names where the header says;
##   * that a corrupt file is renamed rather than overwritten, and that the old
##     bytes survive the rename byte for byte;
##   * that a second corruption finds `.2` rather than clobbering `.1`;
##   * that a **future** version is neither moved nor overwritten, which is the one
##     case where doing the corruption thing destroys an intact season;
##   * that the write goes through a temporary and a rename, and specifically that
##     the target is **never opened for writing** — check 12 proves that from
##     outside the implementation;
##   * that the corpus file survives the round trip through real I/O.
##
## ## What it cannot prove, stated rather than glossed
##
## **Durability across a power cut.** The writer claims it since issue #173 —
## `fsync_shim.h` syncs the temporary to the medium between the close and the
## rename, F_FULLFSYNC on macOS — but *proving* it needs a VM and a hard power
## cut, which is not something a headless probe does. What this probe can and
## does prove is the process-death half: check 11 simulates a crash between the
## temporary write and the rename. The power-cut claim rests on the sequence
## being what `kart_profile.cpp` says it is, plus the platform call doing what
## its manpage says; neither is measured here, and this line is the record of
## which half is measurement and which is documentation.
##
## ## Nothing here touches the real save
##
## Every file goes under `user://profile_probe/`. A verification tool that wrote to
## `user://profile.save` would destroy the career of whoever ran it before every
## commit, which is a worse version of the bug this whole file is about.

## Scratch, and the reason it is under `user://` rather than in the repository is
## `tuning_probe.gd`'s: a tool that writes into the working tree is one
## `git add -A` away from committing its own temporary files, and CLAUDE.md
## records that happening twice.
const SCRATCH := "user://profile_probe"

## The checked-in corpus. Real captured bytes, append-only, never regenerated —
## `tests/data/saves/README.md` is emphatic about why.
const CORPUS := "res://tests/data/saves/v1.save"

## Half of `tuning.h`'s 1e-6 quantum. Any looser and a comparison passes on a
## value the file cannot represent; any tighter and it fails on one it can.
const HALF_QUANTUM := 5e-7

## 0444. Used by check 12 to prove the target is never opened for writing: POSIX
## `rename` needs write permission on the *directory*, not on the file it
## replaces, so a save that succeeds against a read-only target cannot have opened
## it.
const READ_ONLY := FileAccess.UNIX_READ_OWNER | FileAccess.UNIX_READ_GROUP \
	| FileAccess.UNIX_READ_OTHER

## 0600, to put a file back the way it was found.
const OWNER_RW := FileAccess.UNIX_READ_OWNER | FileAccess.UNIX_WRITE_OWNER

var _lines: Array[String] = []
var _passed := 0
var _failed := 0


func _initialize() -> void:
	# Belt and braces. If the extension is missing entirely this script fails to
	# *parse* — `KartProfile` is a static type below — and the absent conclusion
	# line is the only symptom. What this catches is the other case: an extension
	# that loaded but was built before these two classes were registered.
	if not ClassDB.class_exists("KartProfile") or not ClassDB.class_exists("KartSettings"):
		print("=== profile gate")
		print("    KartProfile/KartSettings are not registered. Build the extension:")
		print("      scons arch=arm64 target=editor")
		quit(1)
		return

	print("=== profile gate")
	print("    user://            %s" % KartProfile.system_path("user://"))
	print("    scratch            %s" % KartProfile.system_path(SCRATCH))
	print("    format version     %d" % KartProfile.FORMAT_VERSION)
	print("")

	_clean_scratch()

	_check_paths()
	_check_ghost_path_refuses_traversal()
	_check_setters_refuse_bad_input()
	_check_round_trip()
	_check_two_saves_are_byte_identical()
	_check_corpus_loads_through_godot()
	_check_no_file_is_a_first_run()
	_check_corruption_is_moved_aside()
	_check_second_corruption_lands_at_two()
	_check_future_version_is_refused_not_moved()
	_check_interrupted_write_leaves_previous_intact()
	_check_target_is_never_opened_for_writing()
	_check_unreadable_is_not_corruption()
	_check_no_temp_file_survives_a_save()
	_check_settings_load_when_the_profile_does_not()
	_check_settings_round_trip()
	_check_settings_defaults_on_a_missing_file()
	_check_settings_tolerate_an_unknown_key()
	_check_settings_clamp_a_wild_value()
	_check_settings_reject_a_profile_file()
	_check_profile_rejects_a_settings_file()
	_check_a_zero_byte_file_is_corrupt()
	_check_a_maximal_profile_fits()
	_check_scratch_left_clean()

	for line in _lines:
		print(line)
	print("")
	print("checks %d passed %d failed" % [_passed + _failed, _failed])
	quit(1 if _failed > 0 else 0)


# --- 1 -------------------------------------------------------------------------

## `user://` resolution is the whole of this class's job on the path side, and the
## thing that goes wrong is silent: a profile written to a path that is not the one
## the ghost loader reads is a career that saves fine and has no ghosts. So the
## three names and the ghost directory are asserted against the base directory
## rather than assumed to follow from it.
func _check_paths() -> void:
	var profile := KartProfile.new()
	var set_ok := profile.set_base_dir(SCRATCH)
	var settings := KartSettings.new()
	settings.set_base_dir(SCRATCH)
	var expected := {
		"profile": SCRATCH + "/profile.save",
		"temp": SCRATCH + "/profile.save.tmp",
		"settings": SCRATCH + "/settings.cfg",
		"ghosts": SCRATCH + "/ghosts",
		"corrupt1": SCRATCH + "/profile.save.corrupt.1",
		"corrupt2": SCRATCH + "/profile.save.corrupt.2",
	}
	var actual := {
		"profile": profile.profile_path(),
		"temp": profile.temp_path(),
		"settings": profile.settings_path(),
		"ghosts": profile.ghost_dir(),
		"corrupt1": profile.corrupt_path(1),
		"corrupt2": profile.corrupt_path(2),
	}
	var wrong := ""
	for key in expected:
		if actual[key] != expected[key]:
			wrong = "%s is %s, expected %s" % [key, actual[key], expected[key]]
			break
	# The settings class resolves its own path independently — it must, or the
	# separation is a comment rather than a fact.
	if wrong.is_empty() and settings.settings_path() != expected["settings"]:
		wrong = "KartSettings resolved %s" % settings.settings_path()
	_ok("paths resolve under the base dir", set_ok and wrong.is_empty(),
		wrong if not wrong.is_empty() else "created %s" % KartProfile.system_path(SCRATCH))


# --- 2 -------------------------------------------------------------------------

## A ghost id comes out of a **user-writable text file** and goes into a path.
## `profile.h` excludes `.` and `/` from a slug for that reason and calls it a
## boundary rather than a style rule; this is the boundary held on the Godot side,
## where the path is actually built. A `../` that got through would make the save
## format a file-deletion primitive.
func _check_ghost_path_refuses_traversal() -> void:
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	var good := profile.ghost_path("ar_fwd_ok_0001")
	var hostile := [
		"../../evil", "..", "a/b", "a.b", "/etc/passwd", "", "-flag", "UPPER",
	]
	var leaked := ""
	for id in hostile:
		if not profile.ghost_path(id).is_empty():
			leaked = "%s resolved to %s" % [id, profile.ghost_path(id)]
			break
	_ok("ghost ids that are not slugs are refused",
		good == SCRATCH + "/ghosts/ar_fwd_ok_0001.ghost" and leaked.is_empty(),
		leaked if not leaked.is_empty() else "%d hostile ids refused, good id -> %s" % [
			hostile.size(), good,
		])


# --- 3 -------------------------------------------------------------------------

## A setter that stores an invalid value is a save that fails to *write* later, a
## long way from the menu that caused it — `format_profile` returns -1 on an
## invalid profile, so the failure surfaces as "could not save" with no field
## named. `profile.h` has the validators; the property here is that this class
## actually calls them and refuses loudly.
func _check_setters_refuse_bad_input() -> void:
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	var refusals := {
		"nationality lower case": profile.set_nationality("gbr"),
		"nationality too long": profile.set_nationality("GBRX"),
		"number zero": profile.set_driver_number(0),
		"number past 999": profile.set_driver_number(1000),
		"livery traversal": profile.set_livery("../evil"),
		"livery upper case": profile.set_livery("Works_Blue"),
		"season past the ceiling": profile.set_career_season(99),
		"season negative": profile.set_career_season(-1),
		"round past the ceiling": profile.set_career_round(99),
		"class out of the enum": profile.set_career_class(7),
		"layout out of the enum": profile.set_best("autumn_ridge", 7, KartProfile.CLASS_OK,
			48.0, "g1"),
		"zero lap time": profile.set_best("autumn_ridge", KartProfile.LAYOUT_FORWARD,
			KartProfile.CLASS_OK, 0.0, "g1"),
		"negative points": profile.add_standing("turney_joe", -1.0),
		"standing id not a slug": profile.add_standing("Turney Joe", 10.0),
	}
	var accepted := ""
	for name in refusals:
		if refusals[name]:
			accepted = "%s was accepted" % name
			break
	# And the accepting half, because a validator that refuses everything is not a
	# validator. The name is sanitized rather than refused, which is `profile.h`'s
	# deliberate call, so a dirty name returns false and still stores something.
	var accepts := profile.set_nationality("GBR") and profile.set_driver_number(101) \
		and profile.set_livery("works_blue") and profile.set_career_season(1) \
		and profile.set_career_class(KartProfile.CLASS_KZ2) \
		and profile.add_standing("turney_joe", 88.0)
	var dirty_reported := not profile.set_driver_name("  Turney #1  ")
	var dirty_stored := profile.get_driver_name() == "Turney 1"
	_ok("setters refuse what profile.h rejects",
		accepted.is_empty() and accepts and dirty_reported and dirty_stored,
		accepted if not accepted.is_empty() else
			"%d refused, valid values accepted, '  Turney #1  ' -> '%s'" % [
				refusals.size(), profile.get_driver_name(),
			])


# --- 4 -------------------------------------------------------------------------

## The round trip, field for field including every standings row and every best.
## Asserting "it loaded" would pass on a loader that defaulted the lot, which is
## precisely the best-effort behavior ADR-0042 rejected.
func _check_round_trip() -> void:
	_clean_scratch()
	var written := _populated()
	var saved: Dictionary = written.save()
	var read := KartProfile.new()
	read.set_base_dir(SCRATCH)
	var loaded: Dictionary = read.load()
	var difference := _first_difference(written, read)
	_ok("a saved profile reads back field for field",
		bool(saved["ok"]) and int(loaded["status"]) == KartProfile.LOAD_OK
			and difference.is_empty(),
		difference if not difference.is_empty() else
			"%d bytes, %d standings, %d bests, version %d, %d migrations" % [
				int(saved["bytes"]), read.standing_count(), read.best_count(),
				int(loaded["declared_version"]), int(loaded["migrations"]),
			])


# --- 5 -------------------------------------------------------------------------

## Two saves of one profile must be **byte-identical**, and that is not tidiness:
## ADR-0042 sells the format as diffable, so "what changed in my career" is a
## `git diff` on a copied save. A writer that reordered its records would make
## every line show as changed on a save that changed one, and the feature would be
## worthless while looking like it worked.
func _check_two_saves_are_byte_identical() -> void:
	var profile := _populated()
	profile.save()
	var first := _read_bytes(profile.profile_path())
	profile.save()
	var second := _read_bytes(profile.profile_path())
	# And `to_text()` has to agree with what landed on disk, or a bug report that
	# pastes `to_text()` is describing a different file from the one that broke.
	var as_text := profile.to_text().to_utf8_buffer()
	_ok("two saves are byte-identical",
		first == second and first == as_text and first.size() > 0,
		"%d bytes both times, to_text() agrees" % first.size() if first == second
			and first == as_text else
			"first %d, second %d, to_text %d" % [first.size(), second.size(), as_text.size()])


# --- 6 -------------------------------------------------------------------------

## The corpus file, loaded through the Godot path. `tests/core/test_profile.cpp`
## already parses these bytes from disk with no engine, so what is proved here is
## narrower and it is the part the C++ suite structurally cannot reach: that
## `user://` plumbing, `FileAccess.get_buffer` and the byte-for-byte write do not
## corrupt a real captured file on the way through.
##
## The values asserted are the ones typed into `v1.save` by hand, not values read
## out of it and compared with themselves.
func _check_corpus_loads_through_godot() -> void:
	_clean_scratch()
	var corpus := _read_bytes(CORPUS)
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	_write_bytes(profile.profile_path(), corpus)
	var loaded: Dictionary = profile.load()

	var faults: Array[String] = []
	if int(loaded["status"]) != KartProfile.LOAD_OK:
		faults.append("status %s problem %s line %d" % [
			loaded["status_name"], loaded["problem_name"], int(loaded["line"]),
		])
	if profile.get_driver_name() != "Skirving, Anthony":
		faults.append("name '%s'" % profile.get_driver_name())
	if profile.get_driver_number() != 101:
		faults.append("number %d" % profile.get_driver_number())
	if profile.get_nationality() != "GBR":
		faults.append("nationality '%s'" % profile.get_nationality())
	if profile.get_livery() != "works_blue":
		faults.append("livery '%s'" % profile.get_livery())
	if profile.get_career_class() != KartProfile.CLASS_OK:
		faults.append("class %d" % profile.get_career_class())
	if profile.get_career_season() != 0 or profile.get_career_round() != 2:
		faults.append("season %d round %d" % [
			profile.get_career_season(), profile.get_career_round(),
		])
	if profile.standing_count() != 8:
		faults.append("%d standings" % profile.standing_count())
	elif profile.standing_driver(0) != "turney_joe" \
			or absf(profile.standing_points(0) - 88.0) > HALF_QUANTUM \
			or profile.standing_driver(7) != "powell_zachary" \
			or absf(profile.standing_points(7) - 29.0) > HALF_QUANTUM:
		# Order is meaning: position in the array is position in the championship,
		# so a loader that sorted would pass a count check and lose the table.
		faults.append("standings order %s..%s" % [
			profile.standing_driver(0), profile.standing_driver(7),
		])
	if profile.best_count() != 4:
		faults.append("%d bests" % profile.best_count())
	else:
		var lap := profile.best_time("autumn_ridge", KartProfile.LAYOUT_FORWARD,
			KartProfile.CLASS_KZ2)
		if absf(lap - 44.108900) > HALF_QUANTUM:
			faults.append("autumn_ridge/forward/kz2 lap %.6f" % lap)
		if profile.best_ghost_id("brackwater", KartProfile.LAYOUT_FORWARD,
				KartProfile.CLASS_OK) != "bw_fwd_ok_0002":
			faults.append("brackwater ghost '%s'" % profile.best_ghost_id("brackwater",
				KartProfile.LAYOUT_FORWARD, KartProfile.CLASS_OK))
		if profile.best_ghost_path("brackwater", KartProfile.LAYOUT_FORWARD,
				KartProfile.CLASS_OK) != SCRATCH + "/ghosts/bw_fwd_ok_0002.ghost":
			faults.append("brackwater ghost path wrong")
	# And the round trip is byte-identical to the captured file, which is the
	# assertion `tests/data/saves/README.md` calls the mechanical form of "bump the
	# version on every format change".
	if profile.to_text().to_utf8_buffer() != corpus:
		faults.append("re-serializing v1.save is not byte-identical")

	_ok("the v1 corpus loads through user://", faults.is_empty(),
		", ".join(faults) if not faults.is_empty() else
			"%d bytes, 8 standings, 4 bests, re-serializes identical" % corpus.size())


# --- 7 -------------------------------------------------------------------------

## A first run is not a corruption. Reporting it as one would put a
## `profile.save.corrupt.1` in front of somebody who had never saved, and the
## `.corrupt` names are the one signal in the directory that says "something went
## wrong" — spending it on the ordinary case makes it mean nothing.
func _check_no_file_is_a_first_run() -> void:
	_clean_scratch()
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	profile.set_driver_name("Somebody Else")
	var loaded: Dictionary = profile.load()
	var fresh := profile.get_driver_name() == "New Driver" \
		and profile.get_career_class() == KartProfile.CLASS_OK \
		and profile.get_career_season() == 0 and profile.get_career_round() == 0 \
		and profile.standing_count() == 0 and profile.best_count() == 0
	_ok("a missing file is a first run, not corruption",
		int(loaded["status"]) == KartProfile.LOAD_NO_FILE
			and String(loaded["moved_to"]).is_empty()
			and profile.corrupt_paths().is_empty() and fresh and profile.is_valid()
			and not profile.is_save_blocked(),
		"status %s, nothing moved, fresh profile is valid, %d corrupt files" % [
			loaded["status_name"], profile.corrupt_paths().size(),
		])


# --- 8 -------------------------------------------------------------------------

## The one ADR-0042 wrote the rule for. Something goes wrong at load, the game
## starts fresh, the player plays for an hour, and the **first successful save**
## destroys the only copy of the evidence. So the old file is renamed before
## anything else touches it, the caller is told where it went, and — the part that
## is easy to get half-right — the bytes at the new name are the *original* bytes,
## not a re-serialization and not a truncation.
func _check_corruption_is_moved_aside() -> void:
	_clean_scratch()
	var profile := _populated()
	profile.save()
	# Corrupt it the way a real one is corrupt: a real file with a broken record in
	# the middle, not a file of random noise. `standing` takes two tokens.
	var garbage := profile.to_text().replace("standing turney_joe 88.000000",
		"standing turney_joe not-a-number") .to_utf8_buffer()
	_write_bytes(profile.profile_path(), garbage)

	var read := KartProfile.new()
	read.set_base_dir(SCRATCH)
	read.set_driver_name("Somebody Else")
	var loaded: Dictionary = read.load()
	var moved := String(loaded["moved_to"])
	var preserved := _read_bytes(moved) if not moved.is_empty() else PackedByteArray()
	_ok("a corrupt file is moved aside with its bytes intact",
		int(loaded["status"]) == KartProfile.LOAD_CORRUPT
			and moved == SCRATCH + "/profile.save.corrupt.1"
			and preserved == garbage
			and not FileAccess.file_exists(profile.profile_path())
			and read.get_driver_name() == "New Driver" and read.is_valid()
			and not read.is_save_blocked(),
		"moved to .corrupt.1, %d bytes preserved exactly, problem %s at line %d, "
			% [preserved.size(), loaded["problem_name"], int(loaded["line"])]
			+ "fresh profile started")


# --- 9 -------------------------------------------------------------------------

## The same bug one level up. A corruption handler that always writes `.corrupt.1`
## destroys the previous corruption's evidence, which is the exact failure it was
## built to prevent, and it takes two corruptions to notice. `profile.h` puts the
## index arithmetic in `profile_next_corrupt_index` and the naming in
## `profile_corrupt_index_of`; the property here is that this class listed the
## directory and used them.
func _check_second_corruption_lands_at_two() -> void:
	# Follows check 8 without cleaning: `.corrupt.1` is already on disk and that is
	# the state this check needs.
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	var first := _read_bytes(SCRATCH + "/profile.save.corrupt.1")
	var second_garbage := "version 1\ndriver_name Nobody\n".to_utf8_buffer()
	_write_bytes(profile.profile_path(), second_garbage)
	var loaded: Dictionary = profile.load()
	var moved := String(loaded["moved_to"])
	var listing := profile.corrupt_paths()
	_ok("a second corruption lands at .corrupt.2",
		int(loaded["status"]) == KartProfile.LOAD_CORRUPT
			and moved == SCRATCH + "/profile.save.corrupt.2"
			and _read_bytes(SCRATCH + "/profile.save.corrupt.1") == first
			and _read_bytes(moved) == second_garbage
			and listing.size() == 2 and String(listing[0]).ends_with(".corrupt.1")
			and String(listing[1]).ends_with(".corrupt.2")
			and profile.next_corrupt_index() == 3,
		"moved to .corrupt.2, .corrupt.1 still %d bytes, next index %d, problem %s" % [
			first.size(), profile.next_corrupt_index(), loaded["problem_name"],
		])


# --- 10 ------------------------------------------------------------------------

## The distinction that `ProfileLoadStatus` has a third member for. A save from a
## newer build is **not** an old save, ADR-0042's "never fails because of its age"
## does not reach it, and the failure the separate status prevents is concrete:
## somebody launches an older build, and finds their perfectly intact season filed
## under `profile.save.corrupt.1`.
##
## So: not loaded, not renamed, not overwritten, and the save is blocked so that
## the next one cannot do the overwriting either.
func _check_future_version_is_refused_not_moved() -> void:
	_clean_scratch()
	var profile := _populated()
	profile.save()
	var future := profile.to_text().replace("version %d" % KartProfile.FORMAT_VERSION,
		"version %d" % (KartProfile.FORMAT_VERSION + 3)).to_utf8_buffer()
	_write_bytes(profile.profile_path(), future)

	var read := KartProfile.new()
	read.set_base_dir(SCRATCH)
	read.set_driver_name("Untouched, Driver")
	var loaded: Dictionary = read.load()
	var still_there := _read_bytes(profile.profile_path())
	# And a save must be refused, or the block is decoration.
	var attempted: Dictionary = read.save()
	_ok("a future version is refused and not moved",
		int(loaded["status"]) == KartProfile.LOAD_FUTURE_VERSION
			and int(loaded["declared_version"]) == KartProfile.FORMAT_VERSION + 3
			and String(loaded["moved_to"]).is_empty()
			and read.corrupt_paths().is_empty()
			and still_there == future
			and read.get_driver_name() == "Untouched, Driver"
			and read.is_save_blocked() and not bool(attempted["ok"])
			and _read_bytes(profile.profile_path()) == future,
		"declared version %d, file intact at %d bytes, 0 corrupt files, "
			% [int(loaded["declared_version"]), still_there.size()]
			+ "in-memory profile untouched, save refused")


# --- 11 ------------------------------------------------------------------------

## The interruption, simulated as honestly as a headless probe can. A crash
## between "the temporary is written" and "the rename happens" is exactly a
## `profile.save.tmp` on disk and a `profile.save` that was never touched — so
## that state is built by hand and the target is compared byte for byte with what
## it held before.
##
## **What this does not prove** is durability across a kernel panic or a power
## cut. Since issue #173 the writer syncs the temporary to the medium before the
## rename (`fsync_shim.h`), so the claim exists — but this check cannot cut the
## power, so it proves the process-death half and the file header records the
## split. This check is the process death.
func _check_interrupted_write_leaves_previous_intact() -> void:
	_clean_scratch()
	var profile := _populated()
	profile.save()
	var before := _read_bytes(profile.profile_path())

	# The state a crash mid-write leaves: a partial temporary, no rename.
	var partial := before.slice(0, before.size() / 2)
	_write_bytes(profile.temp_path(), partial)
	var after_interruption := _read_bytes(profile.profile_path())

	# And the recovery: the next real save has to succeed despite the stale
	# temporary and must not leave it behind. A save that refused because a
	# leftover `.tmp` existed would mean one crash makes the game unable to save
	# ever again, which is a worse bug than the one being defended against.
	profile.set_career_round(3)
	var saved: Dictionary = profile.save()
	var after_recovery := _read_bytes(profile.profile_path())
	_ok("an interrupted write leaves the previous file intact",
		after_interruption == before and before.size() > 0
			and bool(saved["ok"]) and after_recovery != before
			and after_recovery == profile.to_text().to_utf8_buffer()
			and not FileAccess.file_exists(profile.temp_path()),
		"target unchanged at %d bytes with a %d-byte stale .tmp present; " % [
			before.size(), partial.size(),
		] + "the next save succeeded (%d bytes) and removed it" % int(saved["bytes"]))


# --- 12 ------------------------------------------------------------------------

## Whether the write is a rename or a direct open, proved from **outside** the
## implementation rather than by reading it.
##
## POSIX `rename` needs write permission on the containing directory and none on
## the file it replaces. So a save against a target at mode 0444 succeeds if the
## implementation renames over it and fails if it opens it — `KartTuning::save_preset`
## is the shape that fails here, and two wave-1 agents flagged it as the wrong
## model for anything holding a record. This check is what stops that shape from
## being copied back in later by somebody simplifying.
func _check_target_is_never_opened_for_writing() -> void:
	_clean_scratch()
	var profile := _populated()
	profile.save()
	var target := profile.profile_path()
	var chmod := FileAccess.set_unix_permissions(target, READ_ONLY)
	var permissions := FileAccess.get_unix_permissions(target)
	profile.set_career_round(4)
	var saved: Dictionary = profile.save()
	var landed := _read_bytes(target)
	# Put it back, or the cleanup cannot remove it on some filesystems.
	FileAccess.set_unix_permissions(target, OWNER_RW)
	if chmod != OK:
		# Not a pass and not a failure of the code under test: the filesystem would
		# not take the permission change, so the measurement was never made. Say so
		# rather than reporting a green check that measured nothing.
		_ok("the target is never opened for writing", false,
			"set_unix_permissions returned %d, so the 0444 case was not measured" % chmod)
		return
	_ok("the target is never opened for writing",
		bool(saved["ok"]) and landed == profile.to_text().to_utf8_buffer(),
		"saved %d bytes over a target at mode 0%o -- a direct open would have failed" % [
			int(saved["bytes"]), int(permissions),
		])


# --- 13 ------------------------------------------------------------------------

## The second case a filesystem adds to `profile.h`'s three statuses, and the one
## where the corruption rule would do harm. If the file cannot be *opened*, the one
## thing known about it is that its contents have never been read — so calling it
## corrupt is a guess, and moving it aside on a guess is how an intact save ends up
## under `.corrupt.1` because of a transient permission problem.
##
## **And the save has to be blocked**, which check 12 is exactly what makes
## necessary: the write is a rename, a rename over an unreadable file succeeds, so
## the very property that makes the save crash-safe is what would let it destroy a
## file nobody has read. The two checks are a pair and neither is complete alone.
func _check_unreadable_is_not_corruption() -> void:
	_clean_scratch()
	var profile := _populated()
	profile.save()
	var target := profile.profile_path()
	var before := _read_bytes(target)
	var chmod := FileAccess.set_unix_permissions(target, 0)

	var read := KartProfile.new()
	read.set_base_dir(SCRATCH)
	read.set_driver_name("Untouched, Driver")
	var loaded: Dictionary = read.load()
	var corrupt_count := read.corrupt_paths().size()
	var blocked := read.is_save_blocked()
	# The in-memory profile must not have been touched by a load that read nothing.
	var untouched := read.get_driver_name() == "Untouched, Driver"
	# The save is attempted while the file is still unreadable, because that is the
	# state it has to be refused in. A rename would otherwise have succeeded here.
	read.set_driver_name("Overwriter, Would-Be")
	var attempted: Dictionary = read.save()
	FileAccess.set_unix_permissions(target, OWNER_RW)
	var after := _read_bytes(target)

	if chmod != OK or FileAccess.file_exists(target) == false:
		_ok("an unreadable file is not corruption", false,
			"set_unix_permissions returned %d, so the case was not measured" % chmod)
		return
	_ok("an unreadable file is not corruption",
		int(loaded["status"]) == KartProfile.LOAD_UNREADABLE
			and String(loaded["moved_to"]).is_empty() and corrupt_count == 0
			and after == before and blocked and untouched and not bool(attempted["ok"]),
		"status %s, 0 corrupt files, %d bytes still in place, profile untouched, "
			% [loaded["status_name"], after.size()] + "save refused")


# --- 14 ------------------------------------------------------------------------

## A successful save leaves no temporary. It is a small property and the reason it
## is held is not tidiness: a `profile.save.tmp` sitting in the user directory is
## indistinguishable from the aftermath of a crash, so a support conversation that
## begins "is there a .tmp file next to it" needs the answer to mean something.
func _check_no_temp_file_survives_a_save() -> void:
	_clean_scratch()
	var profile := _populated()
	var saved: Dictionary = profile.save()
	var files := DirAccess.get_files_at(SCRATCH)
	_ok("a successful save leaves no temporary",
		bool(saved["ok"]) and not FileAccess.file_exists(profile.temp_path())
			and files.size() == 1 and String(files[0]) == "profile.save",
		"the directory holds exactly %s" % ", ".join(files))


# --- 15 ------------------------------------------------------------------------

## The whole reason settings are a separate file, and ADR-0042 says it is not
## tidiness: if `profile.save` is unreadable a player still has to reach the menu,
## read the text and use the pad, so the comfort options, the bindings and the
## accessibility settings in `ARCHITECTURE.md` §18 have to load when the career
## does not. Coupling them means a corrupt career presents an unreadable menu for
## fixing it.
##
## So this check puts a broken career and a good settings file side by side and
## asserts the settings come back **as saved** rather than as defaults — defaults
## would pass a weaker check while proving nothing, because the defaults are also
## what a total failure returns.
func _check_settings_load_when_the_profile_does_not() -> void:
	_clean_scratch()
	var settings := KartSettings.new()
	settings.set_base_dir(SCRATCH)
	settings.set_auto_clutch(false)
	settings.set_auto_shift(false)
	settings.set_camera(KartSettings.CAMERA_COCKPIT)
	settings.set_master_volume_db(-7.5)
	settings.set_steer_deadzone(0.08)
	var stored: Dictionary = settings.save()

	_write_bytes(SCRATCH + "/profile.save", "not a save at all\n".to_utf8_buffer())

	# The boot order a real front end uses: settings first, then the career.
	var fresh_settings := KartSettings.new()
	fresh_settings.set_base_dir(SCRATCH)
	var settings_loaded: Dictionary = fresh_settings.load()
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	var profile_loaded: Dictionary = profile.load()

	var non_default := not fresh_settings.is_auto_clutch() and not fresh_settings.is_auto_shift() \
		and fresh_settings.get_camera() == KartSettings.CAMERA_COCKPIT \
		and absf(fresh_settings.get_master_volume_db() + 7.5) <= HALF_QUANTUM \
		and absf(fresh_settings.get_steer_deadzone() - 0.08) <= HALF_QUANTUM
	_ok("settings load when the career does not",
		bool(stored["ok"]) and bool(settings_loaded["ok"]) and non_default
			and int(settings_loaded["applied"]) == 5
			and int(profile_loaded["status"]) == KartProfile.LOAD_CORRUPT,
		"career %s (%s), settings applied %d/5: camera %s, master %.6f dB, deadzone %.6f" % [
			profile_loaded["status_name"], profile_loaded["problem_name"],
			int(settings_loaded["applied"]), fresh_settings.get_camera_name(),
			fresh_settings.get_master_volume_db(), fresh_settings.get_steer_deadzone(),
		])


# --- 16 ------------------------------------------------------------------------

## Same diffability argument as check 5, and the same temporary-then-rename path,
## because a settings file truncated by a crash is a menu that will not draw.
func _check_settings_round_trip() -> void:
	_clean_scratch()
	var settings := KartSettings.new()
	settings.set_base_dir(SCRATCH)
	settings.set_camera(KartSettings.CAMERA_FREE)
	settings.set_master_volume_db(-3.25)
	settings.save()
	var first := _read_bytes(settings.settings_path())
	settings.save()
	var second := _read_bytes(settings.settings_path())
	var files := DirAccess.get_files_at(SCRATCH)
	_ok("two settings saves are byte-identical",
		first == second and first == settings.to_text().to_utf8_buffer()
			and files.size() == 1 and String(files[0]) == "settings.cfg",
		"%d bytes both times, no temporary left" % first.size())


# --- 17 ------------------------------------------------------------------------

## A first run has no settings file and the defaults are the answer, so `ok` is
## true and `existed` is false. Two separate facts, because a boot script that
## cannot tell them apart cannot decide whether to show a first-run screen.
##
## The defaults themselves are asserted, not just their presence: #40 defaults both
## assists **on** because an unassisted KZ gearbox is `ARCHITECTURE.md` §19's
## "unplayable for newcomers" risk with the mitigation taken out, and 0.15 is the
## deadzone `player_driver.h` has a table for.
func _check_settings_defaults_on_a_missing_file() -> void:
	_clean_scratch()
	var settings := KartSettings.new()
	settings.set_base_dir(SCRATCH)
	settings.set_camera(KartSettings.CAMERA_FREE)
	var loaded: Dictionary = settings.load()
	_ok("a missing settings file gives the defaults",
		bool(loaded["ok"]) and not bool(loaded["existed"]) and int(loaded["applied"]) == 0
			and settings.is_auto_clutch() and settings.is_auto_shift()
			and settings.get_camera() == KartSettings.CAMERA_CHASE
			and absf(settings.get_master_volume_db()) <= HALF_QUANTUM
			and absf(settings.get_steer_deadzone() - 0.15) <= HALF_QUANTUM,
		"ok with existed=false; assists on, camera %s, 0.0 dB, deadzone %.6f" % [
			settings.get_camera_name(), settings.get_steer_deadzone(),
		])


# --- 18 ------------------------------------------------------------------------

## Settings get best-effort loading where the career explicitly does not, and the
## asymmetry is the decision rather than an oversight. A key from a **newer** build
## is the realistic case, and refusing the file over it would leave a downgrading
## player without a readable menu — the one thing the separate file exists to
## guarantee. So an unknown key is skipped with a warning and every key that is
## understood still applies.
##
## What makes that safe, and why the career cannot have it: losing a season is
## unrecoverable, losing a volume setting is a slider.
func _check_settings_tolerate_an_unknown_key() -> void:
	_clean_scratch()
	var settings := KartSettings.new()
	settings.set_base_dir(SCRATCH)
	var text := "settings_version 1\n" \
		+ "assist_auto_clutch false\n" \
		+ "horizon_lock true\n" \
		+ "camera cockpit\n" \
		+ "steer_deadzone not-a-number\n" \
		+ "master_volume_db -2.500000\n"
	_write_bytes(settings.settings_path(), text.to_utf8_buffer())
	var loaded: Dictionary = settings.load()
	var warnings: PackedStringArray = loaded["warnings"]
	_ok("an unknown settings key is skipped, not fatal",
		bool(loaded["ok"]) and int(loaded["applied"]) == 3
			and not settings.is_auto_clutch()
			and settings.get_camera() == KartSettings.CAMERA_COCKPIT
			and absf(settings.get_master_volume_db() + 2.5) <= HALF_QUANTUM
			and absf(settings.get_steer_deadzone() - 0.15) <= HALF_QUANTUM
			and settings.is_auto_shift()
			and warnings.size() == 2,
		"applied 3, %d warnings, the bad deadzone fell back to 0.15 and the "
			% warnings.size() + "unmentioned auto_shift stayed on")


# --- 19 ------------------------------------------------------------------------

## A hand-edited or newer-build value outside the range is clamped rather than
## refused, for the same reason as check 18: one wild number must not cost the
## other four settings. And a slider cannot produce one, so clamping is only ever
## repairing a file somebody edited.
func _check_settings_clamp_a_wild_value() -> void:
	_clean_scratch()
	var settings := KartSettings.new()
	settings.set_base_dir(SCRATCH)
	var text := "settings_version 1\nmaster_volume_db 400.000000\nsteer_deadzone -9.000000\n"
	_write_bytes(settings.settings_path(), text.to_utf8_buffer())
	var loaded: Dictionary = settings.load()
	# And the clamp has to survive a round trip, or the file re-reads to a different
	# value than the one in memory and the format is not idempotent.
	settings.save()
	var again := KartSettings.new()
	again.set_base_dir(SCRATCH)
	again.load()
	_ok("a wild settings value is clamped, not fatal",
		bool(loaded["ok"]) and int(loaded["applied"]) == 2
			and absf(settings.get_master_volume_db() - 6.0) <= HALF_QUANTUM
			and absf(settings.get_steer_deadzone()) <= HALF_QUANTUM
			and absf(again.get_master_volume_db() - 6.0) <= HALF_QUANTUM
			and absf(again.get_steer_deadzone()) <= HALF_QUANTUM,
		"400 dB -> %.6f, -9.0 deadzone -> %.6f, and both survive a re-save" % [
			settings.get_master_volume_db(), settings.get_steer_deadzone(),
		])


# --- 20 ------------------------------------------------------------------------

## The two files cannot be loaded as each other, and the mechanism is that the
## first key differs: `settings_version` here, `version` there. Without that a
## career pointed at `settings.cfg` would half-succeed, which is the silent case
## ADR-0042 spent its whole "alternative rejected" paragraph on.
func _check_settings_reject_a_profile_file() -> void:
	_clean_scratch()
	var profile := _populated()
	profile.save()
	var settings := KartSettings.new()
	settings.set_base_dir(SCRATCH)
	# Feed the career file to the settings loader by name.
	_write_bytes(settings.settings_path(), _read_bytes(profile.profile_path()))
	var loaded: Dictionary = settings.load()
	var warnings: PackedStringArray = loaded["warnings"]
	var said_so := false
	for warning in warnings:
		if String(warning).contains("settings_version"):
			said_so = true
	_ok("a career file loaded as settings says so",
		bool(loaded["ok"]) and int(loaded["applied"]) == 0 and said_so
			and settings.is_auto_clutch() and settings.get_camera() == KartSettings.CAMERA_CHASE,
		"applied 0, %d warnings including the missing settings_version, defaults held"
			% warnings.size())


# --- 21 ------------------------------------------------------------------------

## The mirror. `profile.h`'s lexer requires `version` as the first non-comment
## line, so a settings file fed to the career loader fails on line 1 with
## `version_not_first` rather than binding a few fields and defaulting the rest.
##
## It is reported as a corruption and moved aside, which is correct: from the
## career's point of view a file at `profile.save` that is not a profile is exactly
## a file that does not parse, and preserving it is the right answer whatever it
## turns out to be.
func _check_profile_rejects_a_settings_file() -> void:
	_clean_scratch()
	var settings := KartSettings.new()
	settings.set_base_dir(SCRATCH)
	settings.save()
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	var settings_bytes := _read_bytes(settings.settings_path())
	_write_bytes(profile.profile_path(), settings_bytes)
	var loaded: Dictionary = profile.load()
	var moved := String(loaded["moved_to"])
	_ok("a settings file loaded as a career is refused",
		int(loaded["status"]) == KartProfile.LOAD_CORRUPT
			and String(loaded["problem_name"]) == "version_not_first"
			and int(loaded["line"]) == 4
			and String(loaded["detail"]) == "settings_version"
			and _read_bytes(moved) == settings_bytes,
		"problem %s at line %d naming '%s', moved to .corrupt.1 with %d bytes intact" % [
			loaded["problem_name"], int(loaded["line"]), loaded["detail"],
			settings_bytes.size(),
		])


# --- 22 ------------------------------------------------------------------------

## A zero-byte `profile.save`, which is the file a *non*-atomic writer leaves after
## a crash — `FileAccess::WRITE` truncates on open, so the window between the open
## and the first byte is a window where the save is empty. This project's writer
## cannot produce it, and the file is exactly what an older build or another tool
## would leave, so it has to be handled rather than assumed away.
##
## It is a corruption: the file exists and does not parse. Empty is **not** the same
## as absent, and the distinction matters — an absent file is a first run and a
## zero-byte one is evidence that something went wrong, so it gets preserved.
##
## Worth a check of its own because the code path is a `PackedByteArray` of length
## zero, whose `ptr()` is null, and a loader reached through a null pointer is a
## crash rather than a diagnosis.
func _check_a_zero_byte_file_is_corrupt() -> void:
	_clean_scratch()
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	_write_bytes(profile.profile_path(), PackedByteArray())
	var loaded: Dictionary = profile.load()
	_ok("a zero-byte file is corrupt, not absent",
		int(loaded["status"]) == KartProfile.LOAD_CORRUPT
			and int(loaded["bytes"]) == 0
			and String(loaded["moved_to"]) == SCRATCH + "/profile.save.corrupt.1"
			and FileAccess.file_exists(SCRATCH + "/profile.save.corrupt.1")
			and profile.get_driver_name() == "New Driver",
		"status %s, problem %s, moved to .corrupt.1, fresh profile started" % [
			loaded["status_name"], loaded["problem_name"],
		])


# --- 23 ------------------------------------------------------------------------

## A profile filled to **both** ceilings — every standings row and every best —
## written, read back, and written again identically.
##
## This is the one failure mode that cannot show up in testing and cannot be
## noticed early: `PROFILE_TEXT_CAP` in `profile.h` is a computed ceiling, and if
## the arithmetic is short then `format_profile` returns -1, which `save()` can only
## report as "the profile is not valid" — a misleading message about a profile that
## is perfectly valid and merely large. It would first appear for whoever has played
## the longest, on the day they set one more best lap, as a career that silently
## stops saving.
##
## So the cap is **measured** rather than recomputed here, and the margin is
## printed. A recomputation would just be the same arithmetic a second time.
##
## It also drives the lexer at `PROFILE_MAX_RECORDS` exactly, which is 8 singletons
## plus the two repeated blocks, so an off-by-one in that constant is a
## `too_many_records` on the way back in.
func _check_a_maximal_profile_fits() -> void:
	_clean_scratch()
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	# A 47-character track id and a 31-character ghost id are the widest the format
	# takes, so the records are at their maximum length as well as their maximum
	# count. Anything shorter would measure a smaller document than the format allows.
	var wide_track := "a".repeat(43)
	var refused := 0
	for i in KartProfile.MAX_STANDINGS:
		if not profile.add_standing("driver_%s_%02d" % ["x".repeat(18), i], 1234.5):
			refused += 1
	for i in KartProfile.MAX_BESTS:
		# Distinct keys: 16 track ids x 2 layouts, all in one class.
		var track := "%s_%02d" % [wide_track, i / 2]
		var layout := KartProfile.LAYOUT_FORWARD if i % 2 == 0 else KartProfile.LAYOUT_REVERSE
		if not profile.set_best(track, layout, KartProfile.CLASS_KZ2, 999.999999,
				"ghost_%s_%02d" % ["y".repeat(19), i]):
			refused += 1

	var saved: Dictionary = profile.save()
	var read := KartProfile.new()
	read.set_base_dir(SCRATCH)
	var loaded: Dictionary = read.load()
	var difference := _first_difference(profile, read)
	var bytes := int(saved["bytes"])
	_ok("a profile filled to both ceilings round-trips",
		refused == 0 and bool(saved["ok"]) and bytes > 0
			and int(loaded["status"]) == KartProfile.LOAD_OK and difference.is_empty()
			and read.standing_count() == KartProfile.MAX_STANDINGS
			and read.best_count() == KartProfile.MAX_BESTS
			and _read_bytes(profile.profile_path()) == read.to_text().to_utf8_buffer(),
		difference if not difference.is_empty() else
			"%d standings + %d bests = %d bytes, %d records, nothing refused" % [
				read.standing_count(), read.best_count(), bytes,
				8 + read.standing_count() + read.best_count(),
			])


# --- 24 ------------------------------------------------------------------------

## The probe cleans up after itself. Not housekeeping: check 14 asserts the
## directory holds exactly one file, which is only a measurement if a previous
## run's leftovers are genuinely gone. A stale file passing for this run's output
## is the `SKIP_IMPORT=1` trap in a different costume.
func _check_scratch_left_clean() -> void:
	_clean_scratch()
	_ok("the probe leaves no files behind",
		not DirAccess.dir_exists_absolute(SCRATCH),
		"%s removed" % SCRATCH)


# --- helpers -------------------------------------------------------------------


## A profile with every field populated, including both repeated blocks at more
## than one row, because a round trip over one standing and one best would not
## catch an ordering bug in either.
func _populated() -> KartProfile:
	var profile := KartProfile.new()
	profile.set_base_dir(SCRATCH)
	profile.set_driver_name("Skirving, Anthony")
	profile.set_driver_number(101)
	profile.set_nationality("GBR")
	profile.set_livery("works_blue")
	profile.set_career_class(KartProfile.CLASS_KZ2)
	profile.set_career_season(1)
	profile.set_career_round(2)
	# Classification order, highest first, exactly as a season table arrives.
	profile.add_standing("turney_joe", 88.0)
	profile.add_standing("skirving_anthony", 63.5)
	profile.add_standing("powell_zachary", 29.0)
	# Deliberately out of sorted order on the way in: `profile.h` keeps bests sorted
	# by key so two profiles with the same bests are byte-identical whatever order
	# they were set in, and this is what would catch that promise breaking.
	profile.set_best("brackwater", KartProfile.LAYOUT_FORWARD, KartProfile.CLASS_OK,
		62.330500, "bw_fwd_ok_0002")
	profile.set_best("autumn_ridge", KartProfile.LAYOUT_REVERSE, KartProfile.CLASS_KZ2,
		45.001200, "ar_rev_kz2_0007")
	profile.set_best("autumn_ridge", KartProfile.LAYOUT_FORWARD, KartProfile.CLASS_OK,
		48.412355, "ar_fwd_ok_0001")
	return profile


## The first field on which two profiles differ, or an empty String. Named rather
## than counted, for the reason ADR-0041 gives about a hash mismatch: "they differ"
## says nothing about where.
func _first_difference(a: KartProfile, b: KartProfile) -> String:
	if a.get_driver_name() != b.get_driver_name():
		return "driver_name '%s' vs '%s'" % [a.get_driver_name(), b.get_driver_name()]
	if a.get_driver_number() != b.get_driver_number():
		return "driver_number %d vs %d" % [a.get_driver_number(), b.get_driver_number()]
	if a.get_nationality() != b.get_nationality():
		return "nationality '%s' vs '%s'" % [a.get_nationality(), b.get_nationality()]
	if a.get_livery() != b.get_livery():
		return "livery '%s' vs '%s'" % [a.get_livery(), b.get_livery()]
	if a.get_career_class() != b.get_career_class():
		return "career_class %d vs %d" % [a.get_career_class(), b.get_career_class()]
	if a.get_career_season() != b.get_career_season():
		return "career_season %d vs %d" % [a.get_career_season(), b.get_career_season()]
	if a.get_career_round() != b.get_career_round():
		return "career_round %d vs %d" % [a.get_career_round(), b.get_career_round()]
	if a.standing_count() != b.standing_count():
		return "standing_count %d vs %d" % [a.standing_count(), b.standing_count()]
	for i in a.standing_count():
		if a.standing_driver(i) != b.standing_driver(i):
			return "standing %d is '%s' vs '%s'" % [
				i, a.standing_driver(i), b.standing_driver(i),
			]
		if absf(a.standing_points(i) - b.standing_points(i)) > HALF_QUANTUM:
			return "standing %d points %.6f vs %.6f" % [
				i, a.standing_points(i), b.standing_points(i),
			]
	if a.best_count() != b.best_count():
		return "best_count %d vs %d" % [a.best_count(), b.best_count()]
	for i in a.best_count():
		var left: Dictionary = a.best_at(i)
		var right: Dictionary = b.best_at(i)
		for key in left:
			if key == "lap_time_s":
				if absf(float(left[key]) - float(right[key])) > HALF_QUANTUM:
					return "best %d lap_time_s %.6f vs %.6f" % [
						i, float(left[key]), float(right[key]),
					]
			elif left[key] != right[key]:
				return "best %d %s '%s' vs '%s'" % [i, key, left[key], right[key]]
	return ""


func _read_bytes(path: String) -> PackedByteArray:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return PackedByteArray()
	var bytes := file.get_buffer(file.get_length())
	file.close()
	return bytes


## Deliberately a plain open-and-write, unlike anything in `KartProfile`. This is
## the probe standing in for a crashed process or a text editor, and it has to be
## able to leave the directory in states the real code never would.
func _write_bytes(path: String, bytes: PackedByteArray) -> void:
	var dir := path.get_base_dir()
	if not DirAccess.dir_exists_absolute(dir):
		DirAccess.make_dir_recursive_absolute(dir)
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_error("profile_probe: cannot write %s" % path)
		return
	file.store_buffer(bytes)
	file.close()


func _clean_scratch() -> void:
	# Checked rather than attempted. `DirAccess.get_files_at` on an absent directory
	# is not an error worth handling, but it *prints* one, and two engine ERROR lines
	# at the top of a gate's output is how a clean run gets read as a broken one.
	if not DirAccess.dir_exists_absolute(SCRATCH):
		return
	# Permissions first: checks 12 and 13 leave a file at 0444 or 0000 if they
	# failed part way through, and `remove_absolute` on one of those would leave the
	# directory behind and take check 22 down with it.
	for file in DirAccess.get_files_at(SCRATCH):
		var path := SCRATCH + "/" + file
		FileAccess.set_unix_permissions(path, OWNER_RW)
		DirAccess.remove_absolute(path)
	for sub in DirAccess.get_directories_at(SCRATCH):
		var path := SCRATCH + "/" + sub
		for file in DirAccess.get_files_at(path):
			DirAccess.remove_absolute(path + "/" + file)
		DirAccess.remove_absolute(path)
	DirAccess.remove_absolute(SCRATCH)


## One result line. A passing check prints its measurement rather than a bare
## PASS — `contact_probe.gd`'s discipline, and the reason is that a gate whose
## output is twenty-four identical words tells a reader nothing about what changed
## between two runs of it.
func _ok(name: String, condition: bool, detail: String = "") -> bool:
	if condition:
		_passed += 1
		_lines.append("check %-52s PASS%s" % [
			name, "" if detail.is_empty() else "   " + detail,
		])
	else:
		_failed += 1
		_lines.append("check %-52s FAIL   %s" % [name, detail])
	return condition
