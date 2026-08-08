extends SceneTree

## The HUD gate: what is on screen while the kart is moving.
##
##     tools/verify/hud.sh
##     tools/verify/hud.sh --case=glyphs,layout
##     tools/verify/hud.sh --break=tofu
##
## `shell.sh` covers the ten menu screens and `session_probe.gd` covers the timer
## underneath. **Nothing covered the overlays a driver actually looks at**, and
## issue #242 area 2 says so in Anthony's own words. This is that gate.
##
## ## Why it needs a window, and why that is not optional
##
## `driving_hud.gd:213` and `timing_hud.gd:128` both begin with
##
##     if DisplayServer.get_name() == "headless": hide(); return
##
## which is correct — under `--headless` every `draw_rect` in them emits its own
## error with a stack trace, about forty a tick, and that once buried a scenario
## report under something that read like a solver fault. But it is also the exact
## reason no headless gate could ever have tested these two files, which is why
## they went five milestones without one. So this probe runs against a **real
## rendering device**, the way `tools/shots/shoot.gd` does, and a window opens for
## the few seconds it takes.
##
## ## What it measures, and why almost none of it is arithmetic
##
## The temptation with an overlay is to re-derive its layout in the gate and
## compare. That is a second owner of every constant in it, and CLAUDE.md has a
## whole entry on what a second owner costs. So geometry here is measured **off
## the rendered frame**:
##
##   * an element's extent is the bounding box of the pixels that change when
##     that node alone is hidden — difference imaging, which needs no constant
##     from the file under test and is correct even if the layout is rewritten;
##   * the g meter's center is found as the pure-white dot it draws at zero g,
##     which is the only saturated white on the frame;
##   * a glyph's shape is the FIG-colored mask inside the cell that changes when
##     the speed changes, which locates itself.
##
## The only numbers read out of `driving_hud.gd` are its palette and one radius,
## and those are read from the script object rather than copied.
##
## ## The negative controls
##
## `--break=<mode>` sabotages one property and the probe then asserts the check
## aimed at it went red **naming the saboteur's own fingerprint** — a pre-existing
## failure does not count as caught, which is the hole the first cut of
## `shell_probe.gd` shipped six times. The exit code is inverted under `--break`.

const DRIVING_HUD_PATH := "res://scripts/ui/driving_hud.gd"
const DRIVING_HUD := preload("res://scripts/ui/driving_hud.gd")

## The shipped canvas. `project.godot` sets 1600x900 and `ShellTheme` explains at
## length why the height is the number that matters. Legibility is quoted at this
## size because it is the size a driver gets.
const DESIGN := Vector2i(1600, 900)

## How close a pixel has to be to a palette entry to count as that color. Godot's
## stills are not byte-identical (ADR-0023, about one run in six differs by a mean
## of 2/255) so nothing here may compare exactly; 6/255 is comfortably above that
## floor and far below the distance between any two colors this file keys on.
const COLOR_TOL := 6.0 / 255.0

## A pixel counts as "changed" between two frames at this distance. Same
## reasoning: above ADR-0023's noise, below any real mark.
const DIFF_TOL := 12.0 / 255.0

## Coarse full-frame scans step by this many pixels. A 1600x900 frame is 1.44 M
## pixels and GDScript walks them at roughly a megapixel a second, so a stride of
## 2 turns four full-frame passes from twenty seconds into five. Every measurement
## that needs exact pixels — every glyph — runs on a crop at stride 1.
const COARSE_STRIDE := 2

## Ticks to let `driving_hud.gd`'s two physics-rate accumulators come to rest.
## Its `G_TRAIL_SAMPLES` is 90 and its `SUSTAINED_TICKS` is 60, so 120 clears both
## with margin. Anything that diffs two frames to isolate one figure has to wait
## this long first or it measures the g meter still filling in.
const G_ACCUMULATOR_TICKS := 120

## Frames to wait between changing a value and capturing the frame that shows it.
##
## **Two is not enough and the failure is intermittent**, which is the worst kind.
## The overlay redraws from `_physics_process`, `queue_redraw()` lands on the next
## drawn frame, and `get_texture()` returns the last *completed* frame — so a
## capture taken too soon shows the previous value. At two frames this gate
## reported digits `2` and `3` as pixel-identical on one run in three and passed on
## the others, which reads as a rendering bug in the file under test and is a race
## in the file doing the testing. Same family as CLAUDE.md's
## `Input.parse_input_event` entry, where every assertion made immediately after a
## send reads the previous press.
const CAPTURE_SETTLE := 8

var _args := {}
var _cases: PackedStringArray = []
var _break := ""
var _passes := 0
var _failures: PackedStringArray = []
## Every verdict by name, so a `--break` mode can demand that the check aimed at
## it went red rather than accepting any red at all.
var _results: Dictionary = {}
var _break_caught := false
var _break_evidence := ""
var _done := false

## Three layers, not one. The background has to stay still while an overlay moves
## — difference imaging is meaningless otherwise — and a `--break` mode moves an
## overlay by translating **its layer**, because setting `position` on a Control
## whose `_ready` calls `set_anchors_preset(PRESET_FULL_RECT)` does not survive the
## anchor pass: measured, the timing panel stopped drawing entirely rather than
## moving, and the sabotage went through unnoticed as a result.
var _layer: CanvasLayer
var _hud_layer: CanvasLayer
var _timing_layer: CanvasLayer
var _background: ColorRect
var _hud: Control
var _timing: Control
var _stub: StubKart
var _runner: SessionRunner


## Everything `driving_hud.gd` reads off its kart, and nothing else.
##
## A `Node` and not a `RefCounted` only because the HUD keeps it in a `var kart:
## Node`. Every member here is checked against the real `KartBody` by the
## `contract` case, so this cannot quietly drift into testing a kart that does not
## exist — which is the failure mode a stub normally has.
class StubKart extends Node:
	var speed_ms := 0.0
	var rpm := 0.0
	var gear := 0
	var lateral := 0.0
	var longitudinal := 0.0
	var over_rev := false
	var on_limiter := false
	var shifting := false

	func get_engine_rpm() -> float: return rpm
	func get_gear() -> int: return gear
	func get_lateral_g() -> float: return lateral
	func get_longitudinal_g() -> float: return longitudinal
	func is_over_rev() -> bool: return over_rev
	func is_on_limiter() -> bool: return on_limiter
	func is_shifting() -> bool: return shifting
	func get_soft_cut_rpm() -> float: return 14500.0
	func get_hard_cut_rpm() -> float: return 15000.0
	func get_rollover_threshold_g(turning_left: bool) -> float:
		return 2.4336 if turning_left else 2.8061


func _initialize() -> void:
	_args = Cmdline.parse()

	if Cmdline.as_bool(_args, "list-breaks", false):
		for mode in ["tofu", "overlap", "offscreen", "frozen", "contract", "unbound"]:
			print("break-mode %s" % mode)
		_done = true
		quit(0)
		return

	_break = Cmdline.as_string(_args, "break", "")
	var requested := Cmdline.as_string(_args, "case", "")
	_cases = requested.split(",", false) if requested != "" else PackedStringArray()

	_run()


func _process(_delta: float) -> bool:
	return _done


# --- the run ------------------------------------------------------------------


func _run() -> void:
	# CLAUDE.md: headless the root viewport is 1600x1600, not 1600x900, because the
	# window collapses to its 64x64 minimum and `stretch/aspect="expand"` squares
	# the canvas off against it. That trap passed a focused control 643 px below the
	# real floor in `shell_probe.gd`. This probe runs windowed so it does not apply,
	# but the whole point here is *where things sit*, so the canvas is pinned
	# explicitly and the measured size is printed rather than assumed.
	root.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_IGNORE
	root.content_scale_size = DESIGN
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0

	print("==> HUD gate")
	print("    display server   %s" % DisplayServer.get_name())
	if DisplayServer.get_name() == "headless":
		# Not a failure to diagnose from the inside: both overlays disable
		# themselves here by design, so a headless run measures a blank frame and
		# would report every check green. Refuse instead.
		push_error("hud_probe: this gate needs a rendering device; run it through tools/verify/hud.sh, not --headless")
		_done = true
		quit(2)
		return

	await _build_fixture()
	print("    viewport         %.0fx%.0f" % [
		root.get_visible_rect().size.x, root.get_visible_rect().size.y])
	print("    font source      %s" % ShellTheme.font_source())

	if _wants("contract"):
		_case_contract()
	if _wants("glyphs"):
		await _case_glyphs()
	if _wants("layout"):
		await _case_layout()
	if _wants("legibility"):
		await _case_legibility()
	if _wants("update"):
		await _case_update()
	if _wants("wiring"):
		await _case_wiring()

	_report()


func _wants(name: String) -> bool:
	return _cases.is_empty() or _cases.has(name)


func _build_fixture() -> void:
	_layer = CanvasLayer.new()
	_layer.layer = 1
	root.add_child(_layer)
	_hud_layer = CanvasLayer.new()
	_hud_layer.layer = 2
	root.add_child(_hud_layer)
	_timing_layer = CanvasLayer.new()
	_timing_layer.layer = 3
	root.add_child(_timing_layer)

	# A flat, opaque, unchanging ground. Difference imaging needs the pixels under
	# the overlay to be identical between two captures, and a 3D scene is not — TAA
	# and SSIL converge over frames, so the world would show up as "the HUD changed"
	# in every diff. This is also why the fixture is not a driveable scene.
	_background = ColorRect.new()
	_background.color = Color(0.22, 0.24, 0.27)
	_background.set_anchors_preset(Control.PRESET_FULL_RECT)
	_layer.add_child(_background)

	_stub = StubKart.new()
	root.add_child(_stub)

	_hud = DRIVING_HUD.new()
	_hud.name = "DrivingHud"
	_hud_layer.add_child(_hud)
	_hud.bind(_stub)

	# An unconfigured runner draws its header and its refusal, which is enough to
	# measure where the timing panel sits and how big its figures are. The lap rows
	# themselves need a session with a course under it, and the `wiring` case gets
	# those from the real test track rather than from a mock — a mock lap time would
	# be this file inventing the one number `timing_hud.gd` exists to not invent.
	_runner = SessionRunner.new()
	root.add_child(_runner)
	_timing = TimingHud.new()
	_timing.name = "TimingHud"
	_timing_layer.add_child(_timing)
	_timing.bind(_runner)

	await _settle(4)


func _settle(frames: int) -> void:
	for _i in frames:
		await process_frame


func _grab() -> Image:
	await RenderingServer.frame_post_draw
	return root.get_texture().get_image()


# --- case: contract -----------------------------------------------------------


## Every `kart.<member>` the HUD reads must exist on a real `KartBody`, and every
## `kz_reference()` key it asks for must exist in the real dictionary.
##
## This is the `Dictionary.get(key, default)` family, which CLAUDE.md records
## costing a milestone of dead-straight front wheels: a renamed key read with a
## default does not fail, it draws a zero forever. `driving_hud.gd:240-243` reads
## four `kz_reference()` keys exactly that way. They all resolve today; this is the
## check that says so tomorrow.
##
## The member list is **parsed out of the HUD's own source** rather than typed
## here, so a new read joins the check without a second edit — a hand-maintained
## list is the second owner this whole file is trying not to become.
func _case_contract() -> void:
	var source := FileAccess.get_file_as_string(DRIVING_HUD_PATH)
	if _break == "contract":
		# The sabotage is a read of a getter that does not exist, spelled the way a
		# renamed getter would arrive. The verdict below has to name it.
		source += "\nfunc _sabotage() -> void: var x = kart.get_wheel_temperature()\n"

	var members := _scan(source, "kart\\.([a-zA-Z_][a-zA-Z0-9_]*)")
	var body := KartBody.new()
	var properties: PackedStringArray = []
	for entry in body.get_property_list():
		properties.append(String(entry["name"]))

	var missing: PackedStringArray = []
	for member in members:
		if not body.has_method(member) and not properties.has(member):
			missing.append(member)
	body.free()

	print("    contract: %d distinct kart reads in driving_hud.gd" % members.size())
	_check("contract/kart-surface", missing.is_empty(),
		"driving_hud.gd reads %s off the kart and KartBody publishes no such member"
			% ", ".join(missing))
	if _break == "contract":
		# The fingerprint is the saboteur's own name in the failure, not merely a
		# red: a pre-existing missing member would otherwise read as a catch.
		_catch("contract", _went_red("contract/kart-surface")
			and missing.has("get_wheel_temperature"),
			"missing=%s" % ", ".join(missing))
		return

	var keys := _scan(source, "reference\\.get\\(\"([a-z_]+)\"")
	var reference: Dictionary = KartCore.kz_reference()
	var absent: PackedStringArray = []
	for key in keys:
		if not reference.has(key):
			absent.append(key)
	print("    contract: %d kz_reference() keys read with a default" % keys.size())
	_check("contract/kz-reference-keys", absent.is_empty(),
		"driving_hud.gd defaults %s, which KartCore.kz_reference() does not publish"
			% ", ".join(absent))


## Distinct capture-group matches, in order.
func _scan(source: String, pattern: String) -> PackedStringArray:
	var regex := RegEx.new()
	regex.compile(pattern)
	var found: PackedStringArray = []
	for match in regex.search_all(source):
		var name := match.get_string(1)
		if not found.has(name):
			found.append(name)
	return found


# --- case: glyphs -------------------------------------------------------------


## Can the figures actually be read as the digits they are?
##
## This case exists because a competent reader looked at a real frame of this HUD
## and reported the speed and the lateral g as **missing-glyph boxes**. They are
## not: they are seven-segment zeros on a stationary kart, and a seven-segment
## zero drawn with closed corners is a rectangle outline, which is exactly what a
## tofu box looks like. The diagnosis was wrong and the observation was not, and
## the number that settles it is below.
##
## Two independent halves, because the HUD draws figures two different ways and a
## check that only covered one would have missed whichever half broke:
##
##   * `_seven_string` draws the big figures as polygons — speed, gear, LAT g.
##     Nothing about a font can break these, and no font check would ever have
##     looked at them. They are measured as bitmaps.
##   * `draw_string` draws every label and every small figure through
##     `ThemeDB.fallback_font`. That one genuinely can go tofu, and it is the half
##     the fallback-face trap in CLAUDE.md is about.
func _case_glyphs() -> void:
	# ---- half one: the drawn seven-segment digits ---------------------------
	#
	# The speed field is `"%3d" % round(km/h)`, so speeds 0-9 put each digit in
	# turn at the same place with the same two leading spaces, and nothing else on
	# the overlay moves because every other value comes from a stub member this
	# loop does not touch. The cell locates itself: it is the bounding box of what
	# changes between two of those frames.
	_stub.rpm = 9000.0
	_stub.gear = 3

	var frames: Array[Image] = []
	for digit in 10:
		_stub.speed_ms = float(digit) / 3.6
		await _settle(CAPTURE_SETTLE)
		frames.append(await _grab())

	var cell := _changed_box(frames[8], frames[1], DIFF_TOL, 1)
	if cell.size.x <= 0:
		_check("glyphs/locate", false, "the speed field did not change between 1 and 8 km/h")
		return
	cell = cell.grow(3)
	print("    glyphs: speed cell %dx%d px at (%d, %d)" % [
		cell.size.x, cell.size.y, cell.position.x, cell.position.y])

	var masks: Array[PackedByteArray] = []
	for digit in 10:
		masks.append(_mask(frames[digit], cell, DRIVING_HUD.FIG, COLOR_TOL))

	if _break == "tofu":
		# What tofu *is*, as a bitmap: every value renders as the same shape. Every
		# digit's mask becomes `8`'s, which is every segment lit -- the block a face
		# with no digits draws. The check below has to notice that ten different
		# numbers now look identical; if it still passes it was never comparing the
		# digits at all, which is the failure this whole case was written after.
		for digit in 10:
			masks[digit] = masks[8]

	var lit: PackedInt32Array = []
	var lit_min := 1 << 30
	var lit_max := 0
	for digit in 10:
		var ink_count := _count(masks[digit])
		lit.append(ink_count)
		lit_min = mini(lit_min, ink_count)
		lit_max = maxi(lit_max, ink_count)

	var closest := 1 << 30
	var closest_pair := ""
	for a in 10:
		for b in range(a + 1, 10):
			var distance := _hamming(masks[a], masks[b])
			if distance < closest:
				closest = distance
				closest_pair = "%d/%d" % [a, b]

	# A digit has to differ from every other digit by enough pixels that the
	# difference survives a glance. The threshold is the smallest *legitimate*
	# difference the segment alphabet can produce — `8` against `9` is one segment,
	# the bottom-left `e` — expressed as a fraction of the glyph's own ink rather
	# than as a pixel count, so it does not have to be re-tuned when the size moves.
	var ink := float(lit[8])
	var floor_fraction := 0.06
	print("    glyphs: ten digits, ink %d..%d px, closest pair %s at %d px (%.1f%% of ink)" % [
		lit_min, lit_max, closest_pair, closest, 100.0 * float(closest) / maxf(ink, 1.0)])
	_check("glyphs/digits-distinct", float(closest) > ink * floor_fraction,
		"digits %s differ by only %d px, %.1f%% of a glyph's ink -- they read as the same figure"
			% [closest_pair, closest, 100.0 * float(closest) / maxf(ink, 1.0)])
	if _break == "tofu":
		_catch("tofu", _went_red("glyphs/digits-distinct"),
			"closest pair %s at %d px" % [closest_pair, closest])
		return

	# The finding this case was written for, as a number. A glyph whose ink
	# encloses a hole and carries no interior mark is a rectangle outline, and a
	# rectangle outline is what a missing-glyph box is. `0` is the one that does it.
	for digit in [0, 8]:
		var holes := _holes(masks[digit], cell.size)
		print("    glyphs: `%d` encloses %d hole(s), ink %d px" % [digit, holes, lit[digit]])

	# ---- half two: the font path -------------------------------------------
	#
	# `Font.has_char()` is the analytic handle, and on its own it is the check that
	# cannot fail — CLAUDE.md's `shell_probe.gd` entry is exactly this shape. So the
	# verdict is a *rendered* comparison as well: a character the face genuinely
	# lacks is rasterized, and any character that renders identically to it is tofu.
	var font: Font = ThemeDB.fallback_font
	var size := int(19.0 * root.get_visible_rect().size.y / 1080.0)
	var drawn := "0123456789.-km/hRPMLATgLONGTIPPEAKSUSTOVER"
	var suspect: PackedStringArray = []
	for index in drawn.length():
		var glyph := drawn[index]
		if not font.has_char(glyph.unicode_at(0)):
			suspect.append(glyph)
		elif font.get_string_size(glyph, HORIZONTAL_ALIGNMENT_LEFT, -1.0, size).x <= 0.0:
			suspect.append(glyph)
	print("    glyphs: face %s, %d characters checked at %d px" % [
		font.get_font_name(), drawn.length(), size])
	_check("glyphs/font-has-characters", suspect.is_empty(),
		"the HUD's face has no glyph for %s -- those draw as boxes" % ", ".join(suspect))

	# **The self-test that makes the line above a check rather than a decoration.**
	# `Font.has_char()` reporting true for everything is indistinguishable from a
	# face that has every glyph, and CLAUDE.md records `shell_probe.gd` shipping
	# exactly that shape: a font assertion that was green on every path including
	# the fallback. So ask the same question about a character this face genuinely
	# does not carry. If this comes back true, the check above proved nothing.
	var absent := 0x4E2D  # CJK unified ideograph, not in a Latin UI face
	_check("glyphs/font-check-can-fail", not font.has_char(absent),
		"has_char() is true for U+%04X, so glyphs/font-has-characters cannot fail and proves nothing"
			% absent)


# --- case: layout -------------------------------------------------------------


## Where the overlays actually sit, measured off the frame.
##
## Every extent here is the bounding box of the pixels that change when one node
## is hidden, so nothing in this case knows a single layout constant of the file
## it is testing. Rewrite `driving_hud.gd`'s layout completely and these checks
## still mean what they meant.
func _case_layout() -> void:
	var frame := root.get_visible_rect().size
	var full := Rect2i(Vector2i.ZERO, Vector2i(frame))

	_stub.speed_ms = 140.0 / 3.6
	_stub.rpm = 12000.0
	_stub.gear = 5
	_stub.lateral = 0.0
	_stub.longitudinal = 0.0

	# The sabotages, applied to the live tree before anything is measured. Each
	# moves one overlay and nothing else, so the check aimed at it is the only one
	# that has any reason to change.
	if _break == "overlap":
		# Drop the timing panel down the right-hand edge until it lands on the
		# instrument panel: its extent goes from y 14-65 to y 574-625 against the
		# instrument panel's 568-881, overlapping in x 1320-1543. Nothing else
		# moves and no frame edge is approached, so `layout/no-overlap` is the only
		# check with any reason to change.
		#
		# **The offset is positive, and that is load-bearing.** A negative
		# `CanvasLayer.offset` does not move this layer's contents, it culls them:
		# measured twice on this fixture — the timing layer at (-620, 560) and the
		# driving layer at (0, -520) each drew *nothing at all*, while (500, 0) on
		# the same driving layer moved it correctly. Both sabotages therefore
		# produced an empty extent, no marks to overlap, and a negative control
		# that reported itself as not caught for the right reason and the wrong
		# cause. A sabotage has to be observed doing what it claims before its
		# verdict means anything.
		_timing_layer.offset = Vector2(0.0, 560.0)
	elif _break == "offscreen":
		# Push the instrument panel off the right-hand edge.
		_hud_layer.offset = Vector2(500.0, 0.0)

	await _settle(G_ACCUMULATOR_TICKS)
	var both := await _grab()

	_hud.hide()
	await _settle(CAPTURE_SETTLE)
	var without_driving := await _grab()
	_hud.show()
	_timing.hide()
	await _settle(CAPTURE_SETTLE)
	var without_timing := await _grab()
	_timing.show()
	await _settle(CAPTURE_SETTLE)

	# `--dump=<dir>` writes the frames this case measured. Not a gate output and
	# never compared to a hash (ADR-0023 — Godot stills are not byte-identical);
	# it is here so a number that looks wrong can be looked at.
	var dump := Cmdline.as_string(_args, "dump", "")
	if dump != "":
		DirAccess.make_dir_recursive_absolute(dump)
		both.save_png("%s/layout_both.png" % dump)
		without_driving.save_png("%s/layout_without_driving.png" % dump)
		without_timing.save_png("%s/layout_without_timing.png" % dump)
		print("    layout: frames written to %s" % dump)

	var driving_box := _changed_box(both, without_driving, DIFF_TOL, COARSE_STRIDE)
	var timing_box := _changed_box(both, without_timing, DIFF_TOL, COARSE_STRIDE)

	print("    layout: DrivingHud extent %dx%d at (%d, %d)" % [
		driving_box.size.x, driving_box.size.y, driving_box.position.x, driving_box.position.y])
	print("    layout: TimingHud  extent %dx%d at (%d, %d)" % [
		timing_box.size.x, timing_box.size.y, timing_box.position.x, timing_box.position.y])

	_check("layout/driving-present", driving_box.size.x > 0,
		"the driving HUD drew nothing at all")
	_check("layout/timing-present", timing_box.size.x > 0,
		"the timing HUD drew nothing at all")

	# On screen, and not merely intersecting the screen. A panel whose extent runs
	# to the frame edge has been clipped, and a clipped instrument is one whose
	# missing half nobody can see is missing.
	for pair in [["driving", driving_box], ["timing", timing_box]]:
		var name: String = pair[0]
		var box: Rect2i = pair[1]
		if box.size.x <= 0:
			continue
		# **Within `COARSE_STRIDE` of the border, not exactly on it.** The extent is
		# sampled every `COARSE_STRIDE` pixels, so a panel clipped at the last
		# column reports its last *sampled* column instead — measured 1599 against a
		# frame of 1600, which read as "not clipped" and let the `offscreen`
		# sabotage through unnoticed on the first run of this gate.
		var clipped := box.position.x <= COARSE_STRIDE or box.position.y <= COARSE_STRIDE \
			or box.end.x >= full.end.x - COARSE_STRIDE \
			or box.end.y >= full.end.y - COARSE_STRIDE
		_check("layout/%s-on-screen" % name, not clipped,
			"the %s HUD's extent %s touches the frame edge %s -- it is clipped"
				% [name, box, full])

	# The two overlays must not print on top of each other. They are separate
	# nodes with separate authors and neither knows the other's rect.
	var overlap := driving_box.intersection(timing_box)
	_check("layout/no-overlap", overlap.size.x <= 0 or overlap.size.y <= 0,
		"the driving and timing HUDs overlap over %dx%d px at %s"
			% [overlap.size.x, overlap.size.y, overlap.position])

	if _break == "overlap":
		_catch("overlap", _went_red("layout/no-overlap"),
			"overlap %dx%d px" % [overlap.size.x, overlap.size.y])
		return
	if _break == "offscreen":
		_catch("offscreen", _went_red("layout/driving-on-screen"),
			"extent %s against frame %s" % [driving_box, full])
		return

	# ---- the g meter against the panel, across aspect ratios ---------------
	#
	# These two are drawn by the *same* node, so difference imaging cannot separate
	# them. The g meter locates itself another way: at zero g it draws a pure white
	# dot at its own center, and there is no other saturated white on the frame.
	# Its radius is read off the script rather than copied.
	#
	# Swept across aspect ratios because the panel is centered and the meter is
	# pinned to the right edge, so how far apart they are is a function of the
	# window's shape and not of either constant. The shipped window is 16:9; a
	# driver who resizes is not doing anything unusual.
	var radius: float = float(DRIVING_HUD.G_METER_RADIUS)
	var report: PackedStringArray = []
	var shipped_gap := 0
	var collides_at := 0.0
	for ratio: float in [16.0 / 9.0, 1.6, 1.5, 4.0 / 3.0]:
		var size := Vector2i(int(round(900.0 * ratio)), 900)
		root.content_scale_size = size
		DisplayServer.window_set_size(size)
		await _settle(CAPTURE_SETTLE)
		var shot := await _grab()
		var scale := root.get_visible_rect().size.y / 1080.0
		var dot := _white_dot(shot)
		var panel := _changed_box(shot, await _hidden_frame(), DIFF_TOL, COARSE_STRIDE)
		if dot == Vector2i(-1, -1) or panel.size.x <= 0:
			report.append("%.3f: not measured" % ratio)
			continue
		# The meter's own disc, from its center and its own radius.
		var disc := Rect2i(
			dot - Vector2i(int(radius * scale) + 10, int(radius * scale) + 10),
			Vector2i(2 * (int(radius * scale) + 10), 2 * (int(radius * scale) + 10)))
		# The panel is the extent minus the meter's disc; the meter sits to its
		# right, so the panel's right edge is what the disc can run into.
		var gap := disc.position.x - _panel_right_edge(shot)
		report.append("%.3f -> %d px" % [ratio, gap])
		if is_equal_approx(ratio, 16.0 / 9.0):
			shipped_gap = gap
		if gap < 0 and ratio > collides_at:
			collides_at = ratio
	print("    layout: g meter to panel gap by aspect: %s" % ", ".join(report))
	if collides_at > 0.0:
		# Reported, not failed. The shipped window is 16:9 and this is a layout
		# constant, which under this wave's rule is a proposal with numbers rather
		# than an edit -- see the report. What is asserted below is the one the
		# game actually ships at, which is the one a regression would break.
		print("    layout: NOTE the meter overlaps the panel at aspect %.3f and narrower"
			% collides_at)

	root.content_scale_size = DESIGN
	DisplayServer.window_set_size(DESIGN)
	await _settle(3)

	# At the aspect the game ships at. This can fail, and the sweep above says by
	# how much margin: 126 px at 16:9 falling to 2 px at 3:2.
	_check("layout/g-meter-clear-at-shipped-aspect", shipped_gap > 0,
		"the g meter overlaps the instrument panel by %d px at 16:9, the shipped window"
			% -shipped_gap)


## The frame with the driving HUD hidden, at whatever the window currently is.
func _hidden_frame() -> Image:
	_hud.hide()
	await _settle(CAPTURE_SETTLE)
	var shot := await _grab()
	_hud.show()
	await _settle(CAPTURE_SETTLE)
	return shot


## The right edge of the instrument panel, found as the rightmost run of the LCD's
## own light ground. Keyed on a palette entry read off the script, so it does not
## depend on where the panel is.
func _panel_right_edge(image: Image) -> int:
	var box := _color_box(image, DRIVING_HUD.LCD_LIGHT, COLOR_TOL, COARSE_STRIDE)
	return box.end.x if box.size.x > 0 else 0


## The g meter's center: the pure-white dot it draws there at zero g.
func _white_dot(image: Image) -> Vector2i:
	var box := _color_box(image, Color.WHITE, 3.0 / 255.0, 1)
	if box.size.x <= 0 or box.size.x > 40:
		return Vector2i(-1, -1)
	return box.position + box.size / 2


# --- case: legibility ---------------------------------------------------------


## How big the figures are, in pixels, at the size the game actually ships.
##
## The brief asks for a claim that this is legible at 140 km/h and for that claim
## to be supported. Two things have to be separated to answer it honestly:
##
##   * The overlay is **screen space**. At 140 km/h it is exactly as sharp as at
##     rest — nothing about the speed blurs it, and any claim that it does is
##     about the world behind it and not about the figures. So the measurement is
##     a static one and saying otherwise would be dressing it up.
##   * What speed costs is **glance budget**, not sharpness. A driver at 140 km/h
##     covers 38.9 m a second, so the question is how much can be read per glance,
##     and that is a function of figure size and of how many figures there are.
##
## This case reports the sizes. It does not pass or fail a legibility judgement,
## because that is Anthony's at the wheel and REFERENCES.md already says so in as
## many words: *"whether the layout below is actually legible at 100 km/h is
## therefore a judgement made at the wheel and not a sourced fact"*. What it does
## fail on is a figure that has become **smaller than the smallest one that was
## ever looked at**, which is a regression anyone can act on.
func _case_legibility() -> void:
	var frame := root.get_visible_rect().size
	_stub.speed_ms = 140.0 / 3.6
	_stub.rpm = 12000.0
	_stub.gear = 5
	# **Zero g, and a long settle, or this measures the g meter instead.**
	#
	# `driving_hud.gd` accumulates two things at the physics rate that are still
	# moving long after the value that fed them stopped: `_sustained_best` needs
	# `SUSTAINED_TICKS` = 60 ticks before it reports anything at all, and the g
	# trail grows for `G_TRAIL_SAMPLES` = 90. Diffing two frames two ticks apart
	# therefore catches the trail still filling in, and the changed box runs from
	# the speed figure across into the meter -- measured 142 px tall, against a
	# digit that is 43. Held at zero g the trail is a single point and both
	# accumulators are constant, so the only thing that can differ between the two
	# frames below is the speed figure itself.
	_stub.lateral = 0.0
	_stub.longitudinal = 0.0
	await _settle(G_ACCUMULATOR_TICKS)
	var fast := await _grab()

	_stub.speed_ms = 139.0 / 3.6
	await _settle(CAPTURE_SETTLE)
	var slower := await _grab()

	var cell := _changed_box(fast, slower, DIFF_TOL, 1)
	var digit_h := cell.size.y
	var mask := _mask(fast, cell.grow(2), DRIVING_HUD.FIG, COLOR_TOL)
	var stroke := _stroke_width(mask, cell.grow(2).size)

	# Contrast, by the WCAG 2.1 relative-luminance formula. **sourced** —
	# W3C WCAG 2.1, Understanding SC 1.4.3, contrast ratio (L1+0.05)/(L2+0.05).
	# Quoted because a positive LCD is dark ink on a pale ground and the whole
	# reason the reference unit works in sunlight is that ratio.
	var contrast := _contrast(DRIVING_HUD.FIG, DRIVING_HUD.LCD_LIGHT)

	print("    legibility: at %dx%d" % [frame.x, frame.y])
	print("      speed digit      %d px tall, stroke %d px" % [digit_h, stroke])
	print("      figure/ground    %.2f:1 contrast (WCAG 2.1 relative luminance)" % contrast)
	# A 1600x900 window on a 27-inch 16:9 display is 597 mm wide, so 1600 px spans
	# 597 mm and one pixel is 0.373 mm. At a 700 mm desk viewing distance a figure
	# of h px subtends 2*atan(h*0.373/2/700) radians. **estimated** — the panel
	# size and the viewing distance are both assumptions about Anthony's desk and
	# nothing here sources them; they are stated so the arithmetic can be redone
	# for a different desk rather than hidden inside a verdict.
	var mm_per_px := 597.0 / 1600.0
	var arcmin := rad_to_deg(2.0 * atan(float(digit_h) * mm_per_px * 0.5 / 700.0)) * 60.0
	print("      subtends         %.1f arcmin (est. 27in 16:9 at 700 mm)" % arcmin)

	# The regression floor. `driving_hud.gd` was reviewed and shot at these sizes;
	# the number below is the one measured on the frame this gate was written
	# against, and it is here so a layout edit that halves a figure is caught by
	# something other than a driving session. It is a floor and not a target.
	_check("legibility/speed-not-shrunk", digit_h >= 34,
		"the speed digit measures %d px tall; it was 43 px when this gate was written"
			% digit_h)
	_check("legibility/contrast", contrast >= 4.5,
		"figure against ground is %.2f:1, below WCAG 2.1's 4.5:1 for body text" % contrast)


## The median horizontal run length of ink, which is a segment's thickness.
func _stroke_width(mask: PackedByteArray, size: Vector2i) -> int:
	var runs: PackedInt32Array = []
	for y in size.y:
		var run := 0
		for x in size.x:
			if mask[y * size.x + x] != 0:
				run += 1
			elif run > 0:
				runs.append(run)
				run = 0
		if run > 0:
			runs.append(run)
	if runs.is_empty():
		return 0
	var sorted := Array(runs)
	sorted.sort()
	return int(sorted[sorted.size() / 2])


## WCAG 2.1 relative luminance and contrast ratio.
func _contrast(a: Color, b: Color) -> float:
	var la := _luminance(a)
	var lb := _luminance(b)
	return (maxf(la, lb) + 0.05) / (minf(la, lb) + 0.05)


func _luminance(color: Color) -> float:
	var channels := [color.r, color.g, color.b]
	var linear: Array[float] = []
	for value: float in channels:
		linear.append(value / 12.92 if value <= 0.03928 else pow((value + 0.055) / 1.055, 2.4))
	return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]


# --- case: update -------------------------------------------------------------


## Does the thing on screen actually track the solver, at the resolution it claims?
##
## The speed is drawn as `round(speed_ms * 3.6)`, so the honest test is that it
## changes when that integer changes and **does not** change when it does not. A
## check that only asserted "the pixels moved" would pass an overlay wired to a
## clock.
func _case_update() -> void:
	_stub.rpm = 11000.0
	_stub.gear = 4

	_stub.speed_ms = 140.0 / 3.6
	await _settle(CAPTURE_SETTLE)
	var at_140 := await _grab()

	# 140.4 km/h rounds to the same figure. Nothing may move.
	_stub.speed_ms = 140.4 / 3.6
	await _settle(CAPTURE_SETTLE)
	var at_140_4 := await _grab()

	_stub.speed_ms = 141.0 / 3.6
	if _break == "frozen":
		# The sabotage: stop the overlay redrawing. Everything else is untouched, so
		# a check that still goes green is reading its own expectations.
		_hud.set_physics_process(false)
		_stub.speed_ms = 141.0 / 3.6
	await _settle(CAPTURE_SETTLE)
	var at_141 := await _grab()
	if _break == "frozen":
		_hud.set_physics_process(true)

	var same := _changed_count(at_140, at_140_4, DIFF_TOL, 1)
	var moved := _changed_count(at_140, at_141, DIFF_TOL, 1)
	print("    update: 140.0 vs 140.4 km/h -> %d px changed (must be 0)" % same)
	print("    update: 140.0 vs 141.0 km/h -> %d px changed (must be > 0)" % moved)

	_check("update/tracks-solver", moved > 0,
		"the speed figure did not change between 140 and 141 km/h -- the overlay is not live")
	_check("update/quantized", same == 0,
		"%d px changed between two speeds that round to the same figure" % same)
	if _break == "frozen":
		_catch("frozen", _went_red("update/tracks-solver"),
			"%d px changed at 141 km/h" % moved)


# --- case: wiring -------------------------------------------------------------


## The shipped driveable scene really does build the overlay and bind it to its
## own kart — and the numbers the overlay reads agree with the solver's other
## published path.
##
## `ClassDB.class_exists()` is **not** used to guard anything here. It is always
## false for a GDScript `class_name`, and CLAUDE.md records a `shell_probe.gd`
## check that had therefore never run and reported PASS having looked at nothing.
func _case_wiring() -> void:
	var packed: PackedScene = load("res://scenes/game/proving_ground.tscn")
	if packed == null:
		_check("wiring/scene-loads", false, "proving_ground.tscn did not load")
		return
	var scene := packed.instantiate()
	root.add_child(scene)
	await _settle(20)

	var huds := scene.find_children("DrivingHud", "", true, false)
	print("    wiring: proving_ground.tscn builds %d DrivingHud node(s)" % huds.size())
	_check("wiring/hud-built", huds.size() == 1,
		"proving_ground.tscn built %d driving HUDs, expected exactly 1" % huds.size())

	var karts := scene.find_children("", "KartBody", true, false)
	_check("wiring/kart-present", karts.size() == 1,
		"proving_ground.tscn built %d KartBody nodes, expected exactly 1" % karts.size())

	if huds.size() == 1 and karts.size() == 1:
		var overlay: Node = huds[0]
		var kart: Node = karts[0]
		if _break == "unbound":
			# Rebind the shipped scene's overlay to a kart that is not the scene's.
			# This is the shape of the real defect it guards: an overlay that is
			# present, visible and reading somebody else's numbers.
			overlay.set("kart", _stub)
		_check("wiring/hud-bound", overlay.get("kart") == kart,
			"the driving HUD is bound to %s, not to the scene's own KartBody"
				% overlay.get("kart"))
		_check("wiring/hud-visible", overlay.is_visible_in_tree(),
			"the driving HUD is in the tree and not visible")
		if _break == "unbound":
			_catch("unbound", _went_red("wiring/hud-bound"),
				"bound to %s" % overlay.get("kart"))
			scene.queue_free()
			await _settle(2)
			return

		# The two published paths off the same solver tick must agree. `telemetry()`
		# builds a Dictionary and the getters are the hot path that avoids it;
		# `driving_hud.gd` reads the getters and every other instrument reads the
		# Dictionary, so a disagreement is two instruments showing two numbers.
		#
		# Indexed with `[]` and never with `.get(key, default)`. CLAUDE.md:
		# a renamed key read with a default draws a zero forever.
		var telemetry: Dictionary = kart.call("telemetry")
		var pairs := [
			["engine_rpm", kart.call("get_engine_rpm")],
			["gear", kart.call("get_gear")],
			["lateral_g", kart.call("get_lateral_g")],
			["longitudinal_g", kart.call("get_longitudinal_g")],
			["speed_ms", kart.get("speed_ms")],
		]
		var disagreed: PackedStringArray = []
		for pair in pairs:
			var key: String = pair[0]
			var through_getter: float = float(pair[1])
			var through_dictionary := float(telemetry[key])
			if absf(through_getter - through_dictionary) > 1e-6:
				disagreed.append("%s (getter %.6f, telemetry %.6f)"
					% [key, through_getter, through_dictionary])
		print("    wiring: %d solver values cross-checked getter against telemetry()" % pairs.size())
		_check("wiring/values-agree", disagreed.is_empty(),
			"the getters and telemetry() disagree on %s" % ", ".join(disagreed))

	scene.queue_free()
	await _settle(2)


# --- image helpers ------------------------------------------------------------
#
# All of these walk `Image.get_data()` rather than calling `get_pixel`, which is a
# bound call per pixel and about eight times slower over a frame this size.


func _bytes(image: Image) -> PackedByteArray:
	return image.get_data()


## Bounding box of the pixels that differ between two frames.
func _changed_box(a: Image, b: Image, tolerance: float, stride: int) -> Rect2i:
	var width := a.get_width()
	var height := a.get_height()
	if b.get_width() != width or b.get_height() != height:
		return Rect2i()
	var da := _bytes(a)
	var db := _bytes(b)
	var channels := da.size() / (width * height)
	var cutoff := int(tolerance * 255.0)
	var min_x := width
	var min_y := height
	var max_x := -1
	var max_y := -1
	var y := 0
	while y < height:
		var x := 0
		while x < width:
			var at := (y * width + x) * channels
			if absi(da[at] - db[at]) > cutoff \
					or absi(da[at + 1] - db[at + 1]) > cutoff \
					or absi(da[at + 2] - db[at + 2]) > cutoff:
				min_x = mini(min_x, x)
				min_y = mini(min_y, y)
				max_x = maxi(max_x, x)
				max_y = maxi(max_y, y)
			x += stride
		y += stride
	if max_x < 0:
		return Rect2i()
	return Rect2i(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1)


## How many pixels differ between two frames.
func _changed_count(a: Image, b: Image, tolerance: float, stride: int) -> int:
	var width := a.get_width()
	var height := a.get_height()
	if b.get_width() != width or b.get_height() != height:
		return -1
	var da := _bytes(a)
	var db := _bytes(b)
	var channels := da.size() / (width * height)
	var cutoff := int(tolerance * 255.0)
	var total := 0
	var y := 0
	while y < height:
		var x := 0
		while x < width:
			var at := (y * width + x) * channels
			if absi(da[at] - db[at]) > cutoff \
					or absi(da[at + 1] - db[at + 1]) > cutoff \
					or absi(da[at + 2] - db[at + 2]) > cutoff:
				total += 1
			x += stride
		y += stride
	return total


## Bounding box of the pixels close to one color.
func _color_box(image: Image, color: Color, tolerance: float, stride: int) -> Rect2i:
	var width := image.get_width()
	var height := image.get_height()
	var data := _bytes(image)
	var channels := data.size() / (width * height)
	var cutoff := int(tolerance * 255.0)
	var target := [int(color.r * 255.0), int(color.g * 255.0), int(color.b * 255.0)]
	var min_x := width
	var min_y := height
	var max_x := -1
	var max_y := -1
	var y := 0
	while y < height:
		var x := 0
		while x < width:
			var at := (y * width + x) * channels
			if absi(data[at] - target[0]) <= cutoff \
					and absi(data[at + 1] - target[1]) <= cutoff \
					and absi(data[at + 2] - target[2]) <= cutoff:
				min_x = mini(min_x, x)
				min_y = mini(min_y, y)
				max_x = maxi(max_x, x)
				max_y = maxi(max_y, y)
			x += stride
		y += stride
	if max_x < 0:
		return Rect2i()
	return Rect2i(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1)


## A 1-byte-per-pixel mask of a crop: 1 where the pixel is close to `color`.
func _mask(image: Image, region: Rect2i, color: Color, tolerance: float) -> PackedByteArray:
	var width := image.get_width()
	var height := image.get_height()
	var data := _bytes(image)
	var channels := data.size() / (width * height)
	var cutoff := int(tolerance * 255.0)
	var target := [int(color.r * 255.0), int(color.g * 255.0), int(color.b * 255.0)]
	var out := PackedByteArray()
	out.resize(region.size.x * region.size.y)
	for y in region.size.y:
		for x in region.size.x:
			var px := region.position.x + x
			var py := region.position.y + y
			var hit := 0
			if px >= 0 and py >= 0 and px < width and py < height:
				var at := (py * width + px) * channels
				if absi(data[at] - target[0]) <= cutoff \
						and absi(data[at + 1] - target[1]) <= cutoff \
						and absi(data[at + 2] - target[2]) <= cutoff:
					hit = 1
			out[y * region.size.x + x] = hit
	return out


func _count(mask: PackedByteArray) -> int:
	var total := 0
	for value in mask:
		if value != 0:
			total += 1
	return total


func _hamming(a: PackedByteArray, b: PackedByteArray) -> int:
	var total := 0
	for index in mini(a.size(), b.size()):
		if a[index] != b[index]:
			total += 1
	return total


## How many holes the ink encloses — background regions with no path to the edge.
##
## CLAUDE.md uses Euler characteristic as the cheap check on generated geometry;
## this is the same idea on a glyph bitmap. It is the number that separates a
## figure from a box: a rectangle outline encloses one hole and carries no
## interior mark, which is exactly what a missing-glyph box looks like.
func _holes(mask: PackedByteArray, size: Vector2i) -> int:
	# Flood the background inward from the border; whatever background is left is
	# enclosed. Counting the remaining regions gives the hole count.
	var seen := PackedByteArray()
	seen.resize(size.x * size.y)
	var queue: Array[int] = []
	var edge_rows: PackedInt32Array = [0, size.y - 1]
	var edge_columns: PackedInt32Array = [0, size.x - 1]
	for x in size.x:
		for y in edge_rows:
			var at := y * size.x + x
			if mask[at] == 0 and seen[at] == 0:
				seen[at] = 1
				queue.append(at)
	for y in size.y:
		for x in edge_columns:
			var at := y * size.x + x
			if mask[at] == 0 and seen[at] == 0:
				seen[at] = 1
				queue.append(at)
	while not queue.is_empty():
		var at: int = queue.pop_back()
		var x := at % size.x
		var y := at / size.x
		for step in [[1, 0], [-1, 0], [0, 1], [0, -1]]:
			var nx: int = x + step[0]
			var ny: int = y + step[1]
			if nx < 0 or ny < 0 or nx >= size.x or ny >= size.y:
				continue
			var next := ny * size.x + nx
			if mask[next] == 0 and seen[next] == 0:
				seen[next] = 1
				queue.append(next)

	var holes := 0
	for start in size.x * size.y:
		if mask[start] != 0 or seen[start] != 0:
			continue
		holes += 1
		seen[start] = 1
		var flood: Array[int] = [start]
		while not flood.is_empty():
			var at: int = flood.pop_back()
			var x := at % size.x
			var y := at / size.x
			for step in [[1, 0], [-1, 0], [0, 1], [0, -1]]:
				var nx: int = x + step[0]
				var ny: int = y + step[1]
				if nx < 0 or ny < 0 or nx >= size.x or ny >= size.y:
					continue
				var next := ny * size.x + nx
				if mask[next] == 0 and seen[next] == 0:
					seen[next] = 1
					flood.append(next)
	return holes


# --- verdicts -----------------------------------------------------------------


func _check(name: String, passed: bool, failure: String) -> void:
	_results[name] = passed
	if passed:
		_passes += 1
		print("    PASS %s" % name)
	else:
		_failures.append("%s: %s" % [name, failure])
		print("    FAIL %s: %s" % [name, failure])


## Did the named check run, and did it go red?
##
## `false` for a check that never ran, which matters: a sabotage that also stops
## its own check from executing has not been caught, it has been hidden.
func _went_red(name: String) -> bool:
	return _results.has(name) and not bool(_results[name])


## A sabotage counts as caught only when the measurement carries the saboteur's
## own fingerprint. A pre-existing red is not a catch — that is the mistake the
## first cut of `shell_probe.gd`'s `--break` made.
func _catch(mode: String, caught: bool, evidence: String) -> void:
	if _break != mode:
		return
	_break_caught = caught
	_break_evidence = evidence


func _report() -> void:
	print("")
	if _break != "":
		var verdict := "CAUGHT" if _break_caught else "NOT CAUGHT"
		print("negative control --break=%s: %s (%s)" % [_break, verdict, _break_evidence])
		_done = true
		# Inverted: 0 means the sabotage was noticed.
		quit(0 if _break_caught else 1)
		return

	print("hud gate: %d checks passed, %d failed" % [_passes, _failures.size()])
	for failure in _failures:
		print("    FAILED %s" % failure)
	_done = true
	quit(0 if _failures.is_empty() else 1)
