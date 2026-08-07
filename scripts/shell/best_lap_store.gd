class_name BestLapStore
extends RefCounted

## The one place a lap time gets written to disk.
##
## Before this file, `grep -rn "KartProfile" scripts/` returned nothing at all:
## `src/session/kart_profile.h` had been built, bound, migrated and probed, and
## **no lap time the game produced had ever been saved.** The hole was not in the
## persistence, it was in the join — same family as `assist_auto_shift`, which
## could be stored from M3c and was never loaded by a driveable scene.
##
## `record(profile, result)` is static because there is nothing to keep between
## calls. The profile is the shell's single `KartProfile`; the result is
## `SessionRunner.result()` as the results sheet received it.
##
## ## The negative control is the most important line in the file
##
## `result()["best_lap_s"]` is **-1.0 when no lap was set**, and `lap_timing.h`
## says why it is negative rather than zero: *a zero best lap sorts first and can
## never be beaten*. So a store that wrote whatever it was handed would, on the
## first session where somebody spun on the out lap, seat an unbeatable
## non-time at the top of that circuit's sheet forever, and every real lap
## afterwards would be reported as slower than it. `has_best` is checked first,
## before anything is read, and `shell_probe.gd` asserts `best_count()` does not
## move.
##
## ## `set_best` returning true does not mean it saved
##
## `kart_profile.h`: *"A slower time on an existing key is ignored and reported as
## success ... `has_best` before and after is how a caller learns whether it
## improved."* Nothing failed — the driver finished a lap and a best that got
## worse is not a best — so `true` covers both outcomes. That is why this file
## reads `best_time()` on both sides of the call rather than trusting the return,
## and why `improved` is a measured difference and not a boolean somebody
## forwarded.
##
## ## The empty ghost id, which does not work and is not this file's to fix
##
## `set_best(track, layout, class, time, ghost_id)` **refuses an empty ghost id**.
## `src/core/profile.h:609` runs `profile_is_slug(ghost_id, ...)` unconditionally
## and `profile_is_slug_within` is false at `len <= 0`; `is_valid()` (line 729) and
## the record parser (line 1743) agree with it. All three. The `best` record has no
## spelling for "there is no ghost".
##
## Measured here rather than recalled — see the report accompanying this file: a
## fresh profile, `set_best("valdirone_nuova", 0, 0, 46.611, "")` returns **false**
## and `best_count()` stays **0**.
##
## The setup screen offers "Ghost: off", so this is reachable from a menu on the
## first session anybody drives, and the symptom is a lap time that silently does
## not save. **The fix is a format decision in `src/core/profile.h` and agent C
## owns it** (`shell_probe.gd`'s "a best lap with no ghost can be recorded" stays
## red until it lands). What this file does is attempt the save anyway and put the
## refusal in `warnings` where the results footer can print it. It does **not**
## invent a placeholder slug to slip past the check: a `-` or a `none` in that
## field is a ghost id that a later screen will try to load a ghost from.

## Every key `record()` returns, at its "nothing happened" value.
##
## `improved` is measured, not reported by `set_best`. `previous_s` and `saved_s`
## are seconds or negative for absent, which is `best_time()`'s own convention.
## `ok` is defined on the *disk*, not on the call: **true means the saved profile
## now holds a best for this key that is at least as good as this session's.** So
## a session that failed to beat a stored best is `ok` with `improved` false —
## which is exactly the pair the footer needs to write "Saved best stands:
## 0:46.402" rather than an error.
##
## A function and not a `const`, because `PackedStringArray()` is a constructor
## call and GDScript rejects one inside a constant expression — *"Assigned value
## for constant isn't a constant expression"*, measured, and it takes the whole
## file down at parse time rather than at the call.
static func _blank() -> Dictionary:
	return {
		"improved": false,
		"previous_s": -1.0,
		"saved_s": -1.0,
		"ok": false,
		"warnings": PackedStringArray(),
	}

## Two `best_time()` readings on either side of `set_best` are the same double
## through the same accessor, so they are bit-identical when nothing changed. The
## comparison is still written with a tolerance rather than `!=`, because the day
## the profile stores milliseconds as an int this file should degrade to "did not
## improve" and not to "improved by 4e-16". A microsecond is four orders of
## magnitude below the millisecond a lap time is quoted to.
const IMPROVEMENT_EPSILON_S := 1.0e-6


## File this session's best lap, if it has one and if it is worth filing.
##
## `result` is `SessionRunner.result()` plus the identity keys, and the identity
## is the part the runner does **not** carry: `result()` publishes `type`,
## `config_hash` and the three `DriverResult` measurements, and nothing that says
## which circuit they were set on. `KartSession` knows — `circuit.gd` calls
## `set_track()`, `set_layout()` and `set_kart_class()` on it — but none of the
## three is copied into the result dictionary.
##
## So this reads `track`, `layout`, `kart_class` and `ghost_id` off the dictionary
## and **refuses without a track id**, loudly, in `warnings`. It is the only
## honest answer: a best lap filed under an empty slug is a best lap nobody can
## look up, and guessing a slug from `track_name` would file "Valdirone Nuova"
## next to the "valdirone_nuova" a correct caller writes.
##
## That refusal is also, as it happens, what keeps `shell_probe.gd`'s synthetic
## 12-lap ledger out of Anthony's real career: the gate delivers `track_name` and
## no `track`, and the results screen runs against the shell's real `KartProfile`.
static func record(profile: KartProfile, result: Dictionary) -> Dictionary:
	var out := _blank()
	var warnings := PackedStringArray()

	if profile == null:
		warnings.append("no profile: the extension is not registered, so nothing can be saved")
		out["warnings"] = warnings
		return out

	# **First, before anything is read.** See the header.
	if not bool(result.get("has_best", false)):
		warnings.append("no timed lap in this session, so nothing was filed")
		out["warnings"] = warnings
		return out

	var lap_s := float(result.get("best_lap_s", -1.0))
	if lap_s <= 0.0:
		# `has_best` said yes and the time says otherwise. That is a contradiction in
		# the caller rather than an empty session, so it is named as one.
		warnings.append("has_best is set but best_lap_s is %.6f, which is not a lap time"
				% lap_s)
		out["warnings"] = warnings
		return out

	var track := String(result.get("track", ""))
	if track.is_empty():
		warnings.append("the result carries no track id, so the lap has nowhere to be "
				+ "filed -- the scene that finished the session must carry `track`, "
				+ "`layout` and `kart_class` alongside `track_name`")
		out["warnings"] = warnings
		return out

	var layout := int(result.get("layout", 0))
	var kart_class := int(result.get("kart_class", 0))
	var ghost_id := String(result.get("ghost_id", ""))

	var had := profile.has_best(track, layout, kart_class)
	var previous := profile.best_time(track, layout, kart_class) if had else -1.0
	out["previous_s"] = previous

	var accepted := profile.set_best(track, layout, kart_class, lap_s, ghost_id)
	if not accepted:
		if ghost_id.is_empty():
			# The one refusal that is expected, structural, and somebody else's to fix.
			warnings.append("set_best refused an empty ghost id, so %.3f s was not filed "
					% lap_s + "-- profile.h's `best` record has no spelling for "
					+ "\"no ghost\" and the fix is in src/core/profile.h")
		else:
			warnings.append("set_best refused %s/%d/%d at %.3f s with ghost \"%s\""
					% [track, layout, kart_class, lap_s, ghost_id])
		out["warnings"] = warnings
		return out

	var now := profile.best_time(track, layout, kart_class)
	out["saved_s"] = now
	# The `has_best`-before-and-after rule, spelled as the measurement it is. A key
	# that had no best improved by definition; one that had a best improved only if
	# the stored number actually moved.
	out["improved"] = (not had) or (now < previous - IMPROVEMENT_EPSILON_S)

	if not bool(out["improved"]):
		# Nothing changed in memory, so there is nothing to write. Writing anyway would
		# be a full profile rewrite -- temp, fsync, rename -- on every lap of every
		# session that did not improve, which is most of them.
		out["ok"] = true
		out["warnings"] = warnings
		return out

	var written: Dictionary = profile.save()
	out["ok"] = bool(written.get("ok", false))
	for line: String in written.get("warnings", PackedStringArray()):
		warnings.append(line)
	if not out["ok"]:
		# `save_block_reason()` is `kart_profile.h`'s answer to "why did nothing
		# happen", and surfacing it is the difference between a footer that says the
		# save failed and one that says the disk is full. It is documented as log text
		# rather than UI text, so it is carried as a warning and not as the sentence.
		var blocked := profile.save_block_reason()
		warnings.append("the profile did not save (error %d)%s" % [
			int(written.get("error", -1)),
			"" if blocked.is_empty() else ": " + blocked,
		])
	out["warnings"] = warnings
	return out


## The footer's sentence for an outcome, or empty when there is nothing to say.
##
## ADR-0044 rule 1: whole authored sentences chosen by a condition, never one
## sentence assembled from fragments. Four outcomes, four strings, and the
## alternative — "New best" + " — " + ("saved" if ok else "not saved") — is the
## construction that rule exists to forbid.
##
## Lives here rather than on the results screen because the store is what knows
## which of the four happened, and a second reader of that dictionary would be a
## second place to get the `improved`/`ok` pair wrong.
static func summary(outcome: Dictionary) -> String:
	var improved := bool(outcome.get("improved", false))
	var ok := bool(outcome.get("ok", false))
	var saved := float(outcome.get("saved_s", -1.0))
	var previous := float(outcome.get("previous_s", -1.0))

	if improved and ok:
		return "New best — saved."
	if improved and not ok:
		return "New best — it could not be written to disk."
	if ok and previous > 0.0:
		return "Saved best stands: %s." % SessionRunner.format_time(previous)
	if ok:
		return "Saved best stands: %s." % SessionRunner.format_time(saved)
	return ""
