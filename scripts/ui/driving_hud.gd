extends Control

## The driving HUD: gear, rpm, speed and g, legible from a couch at 100 km/h.
##
## Issue #73, pulled forward from M6 by [#138](https://github.com/skiretic/kartgame/issues/138),
## which is a sequencing ticket rather than a feature request. Four of M3b's
## remaining tickets — #32, #38, #39, #40 — have acceptance criteria written in a
## driver's language ("visibly lifts", "requires clutch modulation", "lifting off
## in second decelerates hard", "demonstrably harder") and none of them can be
## answered from a scrolling graph while both hands are on a pad. This is the
## instrument those judgements are made with.
##
## ## This is not the telemetry panel and must never become it
##
## `scripts/game/telemetry_panel.gd` is the engineering instrument: twelve
## scrolling graphs and a per-wheel table, correct, dense, and unusable at speed.
## It stays exactly as it is and F3 still opens it. The two answer different
## questions and the whole point of #138 is that the project had only the first
## one. Every temptation to add a channel here belongs in the panel instead.
##
## The test for whether something belongs here is not "is it useful" — everything
## in the panel is useful. It is **"would a driver act on it inside one corner"**.
## Gear, rpm, speed and g pass. Slip ratio does not.
##
## ## The layout is taken from a real kart instrument, from photographs
##
## `ARCHITECTURE.md` §5 item 10: do not model a real part from memory or from
## prose. The first version of this file was drawn from an idea of what a kart
## dash looks like and was wrong in five ways, every one of them visible the
## moment a photograph was opened. What the reference actually shows:
##
##   1. It is a **positive LCD** — dark figures on a pale grey-green transflective
##      ground, not glowing white on black. That is the largest single difference
##      and it is why the real thing is readable in direct sunlight.
##   2. **Gear sits top-left and huge**, with "RPM" set **vertically** beside it.
##   3. The tachometer is a **comb of many thin uniform strokes** over a numeric
##      scale, not a row of chunky segments.
##   4. **Speed is medium and centered**, not a hero figure.
##   5. The lower right is a **2x2 grid** of small labeled values — a caps label
##      above, the figure below.
##
## Five square shift LEDs sit above the screen with **two round alarm lamps** at
## the outer corners. That arrangement is better than the ten-LED sweep this file
## first invented, because the alarms are a separate channel from the shift point:
## one says "change gear", the other says "something is wrong".
##
## `docs/REFERENCES.md` records what was looked at, with URLs and licenses.
##
## **What is deliberately not taken.** The information architecture is functional
## and is what makes this read as a kart instrument. The trade dress is not: no
## maker's mark, no product wordmark, no copy of the bezel silhouette, and no
## buttons — this is a screen-space overlay, and drawing physical buttons on it
## would be pretending it is an object bolted to the monitor.
##
## **Where the sensors differ.** The real unit's lower-right grid is Lambda,
## Pedal, Exhaust and Water. This kart models none of those and its driver needs
## g, so the same grid carries LONG, TIP, PEAK and SUST. The slot the real unit
## gives its largest figure to is the running lap time; M6 owns lap timing, and
## #138 says lateral g is what every M3b judgement reduces to, so lateral g has
## the position until there is a lap time to put there.
##
## ## Reading the solver, not the telemetry Dictionary
##
## Every value here comes from `KartBody`'s individual getters rather than from
## `telemetry()`. `src/vehicle/kart_body.h` says why in its own words:
## `telemetry()` allocates a Dictionary, which is fine for a panel that samples
## only while it is open and wrong for an overlay that draws every frame. The
## getters are the path that header calls "the hot paths that do not want the
## Dictionary".
##
## Nothing here holds its own copy of a solver constant. The powerband edges come
## from `KartCore.kz_reference()`, the limiter and rollover thresholds from the
## body. `proving_ground.gd`'s corner text used to carry its own steering-lock
## constant that had to be kept in step by hand, and that is one copy of a
## single-owner number too many.
##
## ## Colorblind safety, since #73 requires it
##
## No information here is carried by hue alone. The shift LEDs are read by **how
## many are lit**, left to right, and the leftmost is always first. The powerband
## and the ignition cut are **full-height strokes standing clear of the comb**.
## The alarm banner **inverts** — a black bar with pale figures, which on a
## positive LCD is the most violent change available and costs no color at all.
## Run the whole thing through a monochrome filter and nothing is lost.

## The kart. Assigned through `bind()`; `proving_ground.gd` does it.
var kart: Node = null

## Where the tachometer's scale ends, rpm.
##
## Not the hard cut. The comb has to have somewhere to go *past* the limiter, or
## over-rev — which `engine.h` models as a state that damages the engine — is
## drawn off the end of the scale and is invisible exactly when it matters.
const SCALE_HEADROOM_RPM := 600.0

## The g meter's outer radius, in g.
##
## 2.9 rather than the sustained band's 2.0. ADR-0034 established that 2.0-2.5 g
## is a **transient peak** band and that a corner entry legitimately visits it, so
## a meter clipped at 2.0 would peg on every turn-in and stop saying anything. It
## also has to contain the rollover thresholds — 2.4336 g turning left and 2.8061
## turning right — because those are drawn.
const G_METER_MAX := 2.9

## How many g samples the trail keeps. 90 at 120 Hz is 0.75 s.
##
## Not decoration and not a graph. It is the one thing on this overlay that
## distinguishes a **transient** from a **sustained** number, which is the exact
## distinction ADR-0034 spent a milestone establishing: a dot that touches 2.3 g
## and springs back is a corner entry, and a dot that sits there is a kart on two
## wheels. A single instantaneous dot cannot tell those apart, and a driver
## reading one would make ADR-0034's mistake live.
##
## 0.75 s comes from the same arithmetic: ADR-0034's roll table gives 2.5 g
## costing 0.4 degrees of roll at 0.2 s and 2.5 degrees at 0.5 s, so three
## quarters of a second spans the range over which the distinction becomes real.
const G_TRAIL_SAMPLES := 90

## What counts as sustained: held continuously for this many ticks. 60 is 0.5 s.
##
## Given a definition here rather than left to the eye. Without one a driver reads
## the peak figure and reports a transient as a skidpad number, which is precisely
## the error ADR-0034 documents the project making against its own reference band
## for two milestones.
const SUSTAINED_TICKS := 60

## Layout, in pixels at 1080p, scaled by viewport height.
##
## Scaled by height and not by width: on an ultrawide, scaling by width would make
## the gear numeral fill a third of the screen.
const PANEL_W := 1060.0
const PANEL_H := 322.0
const PANEL_MARGIN := 40.0
const SCREEN_INSET := 20.0
const PAD := 16.0
const G_METER_RADIUS := 104.0

## Positive-LCD palette. Figures are dark; unlit pixels on a graphic LCD are
## simply absent, which is why OFF is 5% and not a visible ghost.
const LCD_LIGHT := Color(0.788, 0.812, 0.761)
const LCD_DARK := Color(0.698, 0.725, 0.663)
const FIG := Color(0.082, 0.098, 0.059)
const FIG_DIM := Color(0.082, 0.098, 0.059, 0.32)

## Unlit segments are **invisible**, not faint.
##
## The reference is a graphic LCD, not a segment panel: a pixel that is not
## addressed shows the backlight and nothing else. Drawn at 5% this read as a row
## of grey placeholder boxes in front of every space-padded figure — the speed
## showed a ghost digit beside "33" and the lateral g showed two. That is a
## segment display's look, and copying it here produced an artifact the reference
## does not have.
const FIG_OFF := Color(0.082, 0.098, 0.059, 0.0)
const SURROUND := Color(0.055, 0.059, 0.067)

## The five shift LEDs. Hue reinforces the reading; the count carries it.
const LED_COLORS: PackedColorArray = [
	Color("8f5bd6"), Color("e8443a"), Color("f0a81e"), Color("3fc46a"), Color("37a8dd"),
]
const ALARM_RED := Color("ff3b30")

var _font: Font
var _s := 1.0

var _powerband_min := 0.0
var _powerband_max := 0.0
var _sustained_min := 1.5
var _sustained_max := 2.0
var _soft_cut := 0.0
var _hard_cut := 0.0
var _rollover_left := 0.0
var _rollover_right := 0.0
var _scale_max_rpm := 1.0

var _trail: PackedVector2Array = PackedVector2Array()
var _trail_head := 0
var _trail_filled := 0

## The last `SUSTAINED_TICKS` lateral magnitudes, as a ring. See `_physics_process`
## for why a window and not a counter.
var _lateral_window := PackedFloat32Array()
var _lateral_head := 0
var _lateral_filled := 0
var _sustained_best := 0.0
var _peak_lateral := 0.0

## Seconds since the scene started, for the two blink rates. Advanced in
## `_physics_process` from the fixed step rather than from a wall clock, so a
## still taken at a given tick is reproducible — `shoot.sh`'s whole premise.
var _blink_time := 0.0

## One `StyleBoxFlat` per rounded shape, built once. Godot's `_draw` has no
## rounded-rect call, and building a StyleBox per frame would allocate in the
## draw path of an overlay that redraws at the physics rate.
var _sb_surround := StyleBoxFlat.new()
var _sb_screen := StyleBoxFlat.new()
var _sb_led := StyleBoxFlat.new()


func _ready() -> void:
	# Nothing to draw on, and every primitive below would fail individually.
	#
	# `tools/verify/drive.sh` runs the proving ground under `--headless`, where
	# there is no rendering server: `_draw` is still called, and each `draw_rect`,
	# `draw_circle` and `draw_colored_polygon` in it emits its own error with a
	# stack trace. One tick of this overlay is about forty of them, which buried
	# the scenario report under thousands of lines of trace that looked like a
	# solver fault and were an overlay with no canvas.
	#
	# Guarded here rather than in each caller, so a future headless harness does
	# not have to remember a flag. The F3 telemetry panel has no equivalent problem
	# because it starts hidden and draws nothing until opened.
	if DisplayServer.get_name() == "headless":
		set_physics_process(false)
		hide()
		return

	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_preset(Control.PRESET_FULL_RECT)
	_font = ThemeDB.fallback_font
	_trail.resize(G_TRAIL_SAMPLES)
	_lateral_window.resize(SUSTAINED_TICKS)
	set_physics_process(true)

	_sb_surround.bg_color = SURROUND
	_sb_surround.set_corner_radius_all(10)
	_sb_surround.border_color = Color(1.0, 1.0, 1.0, 0.10)
	_sb_surround.set_border_width_all(1)
	_sb_surround.shadow_color = Color(0.0, 0.0, 0.0, 0.6)
	_sb_surround.shadow_size = 22

	_sb_screen.bg_color = LCD_LIGHT
	_sb_screen.set_corner_radius_all(4)
	_sb_screen.border_color = Color(0.0, 0.0, 0.0, 0.55)
	_sb_screen.set_border_width_all(2)

	_sb_led.set_corner_radius_all(5)

	var reference: Dictionary = KartCore.kz_reference()
	_powerband_min = float(reference.get("powerband_min_rpm", 9000.0))
	_powerband_max = float(reference.get("powerband_max_rpm", 14000.0))
	_sustained_min = float(reference.get("lateral_sustained_g_min", 1.5))
	_sustained_max = float(reference.get("lateral_sustained_g_max", 2.0))

	get_viewport().size_changed.connect(queue_redraw)


## Called once by whoever owns the kart, after the body exists.
##
## Split out of `_ready` because the overlay and the body are built in the same
## function and reading the limiter thresholds off a null kart would leave the
## tachometer silently scaled to nothing.
func bind(body: Node) -> void:
	kart = body
	_soft_cut = kart.get_soft_cut_rpm()
	_hard_cut = kart.get_hard_cut_rpm()
	_rollover_left = kart.get_rollover_threshold_g(true)
	_rollover_right = kart.get_rollover_threshold_g(false)
	_scale_max_rpm = _hard_cut + SCALE_HEADROOM_RPM
	queue_redraw()


## Sampled at the physics rate, not the frame rate.
##
## The trail is a fixed number of *ticks*, so the 0.75 s it covers is 0.75 s
## whatever the frame rate is doing. Sampling in `_process` would shorten the
## trail under load, which is exactly when a driver is most likely to need it.
func _physics_process(delta: float) -> void:
	if kart == null:
		return
	_blink_time += delta

	var lateral: float = kart.get_lateral_g()
	var longitudinal: float = kart.get_longitudinal_g()

	_trail[_trail_head] = Vector2(lateral, longitudinal)
	_trail_head = (_trail_head + 1) % G_TRAIL_SAMPLES
	if _trail_filled < G_TRAIL_SAMPLES:
		_trail_filled += 1

	var magnitude := absf(lateral)
	_peak_lateral = maxf(_peak_lateral, magnitude)

	# Sustained is the **largest floor the kart has held for half a second**:
	# the maximum over time of the minimum over the last `SUSTAINED_TICKS`
	# samples. Every value in the window is at or above the figure reported, which
	# is what "sustained" has to mean for the number to be comparable against
	# §6.4's band.
	#
	# The first version of this counted ticks above the running best and then
	# recorded the **instantaneous** magnitude on the tick the counter tripped.
	# That reports a spike as a skidpad figure: 59 ticks at 0.60 g followed by one
	# single tick at 2.40 g reported 2.40 g sustained, reproduced exactly. It is
	# ADR-0034's error — a transient read as a steady-state number — living inside
	# the widget written to keep a driver from making it, and #32 and #40 are
	# judged off this readout.
	#
	# A linear scan of sixty floats at 120 Hz is 7,200 comparisons a second. A
	# monotonic deque would be asymptotically better and is not worth the code at
	# this size.
	_lateral_window[_lateral_head] = magnitude
	_lateral_head = (_lateral_head + 1) % SUSTAINED_TICKS
	if _lateral_filled < SUSTAINED_TICKS:
		_lateral_filled += 1
	elif _lateral_filled == SUSTAINED_TICKS:
		var lowest := INF
		for sample in _lateral_window:
			lowest = minf(lowest, sample)
		_sustained_best = maxf(_sustained_best, lowest)

	queue_redraw()


func reset_peaks() -> void:
	_peak_lateral = 0.0
	_sustained_best = 0.0
	_lateral_filled = 0
	_lateral_head = 0
	_trail_filled = 0
	_trail_head = 0


func _draw() -> void:
	if kart == null or _scale_max_rpm <= 1.0:
		return

	var frame := get_viewport_rect().size
	_s = frame.y / 1080.0

	var panel := Rect2(
		(frame.x - PANEL_W * _s) * 0.5,
		frame.y - (PANEL_H + PANEL_MARGIN) * _s,
		PANEL_W * _s, PANEL_H * _s)

	draw_style_box(_sb_surround, panel)

	var rpm: float = kart.get_engine_rpm()
	var over_rev: bool = kart.is_over_rev()
	var on_limiter: bool = kart.is_on_limiter()

	_draw_led_strip(panel.position + Vector2(panel.size.x * 0.5, 26.0 * _s), rpm, over_rev)
	_draw_alarm_lamp(panel.position + Vector2(40.0 * _s, 26.0 * _s), on_limiter or over_rev, "1")
	_draw_alarm_lamp(panel.position + Vector2(panel.size.x - 40.0 * _s, 26.0 * _s), over_rev, "2")

	var screen := Rect2(
		panel.position + Vector2(SCREEN_INSET, 50.0) * _s,
		panel.size - Vector2(SCREEN_INSET * 2.0, 70.0) * _s)
	draw_style_box(_sb_screen, screen)
	# A shallow second wash across the lower half. A transflective LCD is not one
	# flat value and a single fill reads as paper.
	draw_rect(Rect2(screen.position + Vector2(0, screen.size.y * 0.5),
		Vector2(screen.size.x, screen.size.y * 0.5)), Color(LCD_DARK, 0.5), true)

	_draw_screen(screen, rpm, over_rev, on_limiter)

	var g_center := Vector2(
		get_viewport_rect().size.x - (G_METER_RADIUS + 82.0) * _s,
		get_viewport_rect().size.y - (G_METER_RADIUS + 158.0) * _s)
	_draw_g_meter(g_center, G_METER_RADIUS * _s)


# --- the LCD ----------------------------------------------------------------


func _draw_screen(screen: Rect2, rpm: float, over_rev: bool, on_limiter: bool) -> void:
	var pad := PAD * _s

	# ---- gear, top-left, with a vertical RPM label --------------------------
	var gear: int = kart.get_gear()
	var gx := screen.position.x + pad + 6.0 * _s
	var gy := screen.position.y + pad
	if gear <= 0:
		# `gearbox.h` numbers neutral 0. A driver reads "N", and a KZ genuinely
		# sits in neutral on the grid, so this is not an edge case.
		_text(Vector2(gx + 40.0 * _s, gy + 96.0 * _s), "N", 118.0, FIG, HORIZONTAL_ALIGNMENT_CENTER, 0.8)
	else:
		_seven(Vector2(gx, gy), 76.0 * _s, 108.0 * _s, str(gear), FIG, FIG_OFF)
	for i in 3:
		_text(Vector2(gx + 92.0 * _s, gy + (26.0 + i * 30.0) * _s), "RPM"[i], 21.0, FIG,
			HORIZONTAL_ALIGNMENT_LEFT, 0.95)

	# ---- comb tachometer ----------------------------------------------------
	#
	# Sixty-two thin uniform strokes. The comb is the reading; the two full-height
	# strokes standing clear of it are landmarks and are not part of it.
	var bx := gx + 118.0 * _s
	var by := screen.position.y + pad + 2.0 * _s
	var bw := screen.end.x - pad - bx
	var bh := 58.0 * _s
	var bars := 62
	var bgap := 2.0 * _s
	var bar_w := (bw - (bars - 1) * bgap) / float(bars)
	var lit := int(round(clampf(rpm / _scale_max_rpm, 0.0, 1.0) * bars))
	for i in lit:
		draw_rect(Rect2(bx + i * (bar_w + bgap), by, bar_w, bh), FIG, true)

	for at: float in [_powerband_min, _soft_cut]:
		var tx := bx + (at / _scale_max_rpm) * bw
		draw_rect(Rect2(tx - 1.5 * _s, by - 4.0 * _s, 3.0 * _s, bh + 8.0 * _s), FIG, true)

	var at_rpm := 0.0
	while at_rpm <= 14000.0:
		var tx := bx + (at_rpm / _scale_max_rpm) * bw
		_text(Vector2(tx, by + bh + 20.0 * _s), str(int(at_rpm / 1000.0)), 17.0, FIG,
			HORIZONTAL_ALIGNMENT_CENTER, 0.9)
		at_rpm += 2000.0

	# ---- speed, centered under the tach -------------------------------------
	var mid_y := screen.position.y + 168.0 * _s
	var speed_x := screen.position.x + screen.size.x * 0.52
	var speed_w := _seven_string(Vector2(speed_x, mid_y), 30.0 * _s, 52.0 * _s, 8.0 * _s,
		"%3d" % int(round(kart.speed_ms * 3.6)), FIG, FIG_OFF, HORIZONTAL_ALIGNMENT_CENTER)
	_text(Vector2(speed_x + speed_w * 0.5 + 8.0 * _s, mid_y + 46.0 * _s), "km/h", 19.0, FIG,
		HORIZONTAL_ALIGNMENT_LEFT, 0.95)

	# The reference puts LAP and BEST here. **Nothing is drawn there until M6**,
	# which owns lap timing — #73's other half. An "absent" placeholder was tried
	# and removed: it landed two pixels from the LAT label below it and the two
	# overprinted, which is worse than an empty region. An instrument with a blank
	# area is honest; an instrument with two labels on top of each other is not.

	# ---- lateral g, the hero figure -----------------------------------------
	var lateral: float = kart.get_lateral_g()
	var lat_text := ("-" if lateral < 0.0 else " ") + "%.2f" % absf(lateral)
	# The label sits **above** its figure, which is what the reference does for
	# every labeled value on the screen and what the 2x2 grid below already does.
	# Below, it was clipped by the screen's own bottom edge.
	var hero_x := screen.position.x + pad + 6.0 * _s
	_text(Vector2(hero_x, screen.end.y - 78.0 * _s), "LAT g", 15.0, FIG_DIM,
		HORIZONTAL_ALIGNMENT_LEFT, 0.9, 1.6)
	_seven_string(Vector2(hero_x, screen.end.y - 72.0 * _s),
		32.0 * _s, 62.0 * _s, 9.0 * _s, lat_text, FIG, FIG_OFF, HORIZONTAL_ALIGNMENT_LEFT)

	# ---- the 2x2 grid, bottom right -----------------------------------------
	#
	# TIP % is this kart's lateral g as a fraction of the threshold it would tip
	# at **in the direction it is currently turning**. `chassis.h` makes those two
	# numbers different — 2.4336 g left against 2.8061 right, because 27 kg of
	# engine, exhaust and radiator hang off the right side — so a single symmetric
	# percentage would overstate the margin in one direction and understate it in
	# the other.
	var threshold := _rollover_left if lateral < 0.0 else _rollover_right
	var tip := 0.0 if threshold <= 0.0 else absf(lateral) / threshold * 100.0
	var cells := [
		["LONG g", "%+.2f" % kart.get_longitudinal_g()],
		["TIP %", "%d" % int(round(tip))],
		["PEAK g", "%.2f" % _peak_lateral],
		["SUST g", "%.2f" % _sustained_best],
	]
	var cw := 150.0 * _s
	var ch := 62.0 * _s
	var c0 := Vector2(screen.end.x - pad - cw * 2.0, screen.end.y - pad - ch * 2.0 + 6.0 * _s)
	for i in cells.size():
		var cell: Array = cells[i]
		var cell_at := c0 + Vector2((i % 2) * cw, (i / 2) * ch)
		_text(cell_at + Vector2(0.0, 16.0 * _s), cell[0], 14.0, FIG_DIM, HORIZONTAL_ALIGNMENT_LEFT, 0.9, 1.2)
		_text(cell_at + Vector2(0.0, 48.0 * _s), cell[1], 34.0, FIG, HORIZONTAL_ALIGNMENT_LEFT, 0.85)

	# ---- alarm banner -------------------------------------------------------
	#
	# Inverted: a black bar with pale figures. On a positive LCD that is the most
	# violent change available and it spends no hue, which is what keeps #73's
	# colorblind requirement satisfied without a second accent.
	var warn := ""
	if over_rev:
		warn = "OVER-REV"
	elif on_limiter:
		warn = "LIMITER"
	elif kart.is_shifting():
		warn = "SHIFT"
	if warn != "" and (not over_rev or int(_blink_time * 6.0) % 2 == 0):
		var bw2 := 210.0 * _s
		var bh2 := 34.0 * _s
		var banner_at := Vector2(speed_x - bw2 * 0.5, mid_y + 22.0 * _s)
		draw_rect(Rect2(banner_at, Vector2(bw2, bh2)), FIG, true)
		_text(banner_at + Vector2(bw2 * 0.5, 25.0 * _s), warn, 22.0, LCD_LIGHT,
			HORIZONTAL_ALIGNMENT_CENTER, 0.9, 3.0)


# --- lamps ------------------------------------------------------------------


## Five square LEDs. **The count carries the reading**, left to right, and the
## leftmost is always first — which is what survives a monochrome filter. Hue only
## reinforces it.
##
## They fill across the powerband rather than across the whole rev range, because
## a shift light that starts at idle is a decoration. Past the soft cut the whole
## strip flashes white, which is a different signal and not a sixth step.
func _draw_led_strip(center: Vector2, rpm: float, over_rev: bool) -> void:
	var span := _soft_cut - _powerband_min
	var fraction := 0.0 if span <= 0.0 else clampf((rpm - _powerband_min) / span, 0.0, 1.0)
	var lit := int(round(fraction * LED_COLORS.size()))
	var flash := (over_rev or rpm > _soft_cut) and int(_blink_time * 14.0) % 2 == 0

	var spacing := 46.0 * _s
	var size := 22.0 * _s
	var total := (LED_COLORS.size() - 1) * spacing
	for i in LED_COLORS.size():
		var at := Vector2(center.x - total * 0.5 + i * spacing - size * 0.5, center.y - size * 0.5)
		var on := flash or i < lit
		_sb_led.bg_color = (Color.WHITE if flash else LED_COLORS[i]) if on else Color(1, 1, 1, 0.06)
		draw_style_box(_sb_led, Rect2(at, Vector2(size, size)))
		if on:
			# Godot's `_draw` has no blur, so the bloom is two translucent rings.
			# Cheaper than a shader and it only has to read as a lit lamp.
			var glow := Color.WHITE if flash else LED_COLORS[i]
			draw_circle(at + Vector2(size, size) * 0.5, size * 0.95, Color(glow, 0.20))
			draw_circle(at + Vector2(size, size) * 0.5, size * 1.35, Color(glow, 0.09))


## The two round alarm lamps at the outer corners.
##
## A separate channel from the shift LEDs, and that separation is the reference's
## idea rather than this file's: one strip says "change gear", these say
## "something is wrong". Lamp 1 is the ignition cut, lamp 2 is over-rev — which
## `engine.h` models as real damage, so it earns its own lamp rather than a
## brighter version of the first.
func _draw_alarm_lamp(center: Vector2, active: bool, label: String) -> void:
	var blink := active and int(_blink_time * 5.0) % 2 == 0
	var radius := 12.0 * _s
	draw_circle(center, radius, ALARM_RED if blink else Color(ALARM_RED, 0.10))
	if blink:
		draw_circle(center, radius * 1.7, Color(ALARM_RED, 0.20))
		draw_circle(center, radius * 2.4, Color(ALARM_RED, 0.08))
	_text(center + Vector2(18.0 * _s, 6.0 * _s), label, 14.0, Color(1, 1, 1, 0.32),
		HORIZONTAL_ALIGNMENT_LEFT, 0.9)


# --- g meter ----------------------------------------------------------------


## The friction circle, which is the honest way to show two accelerations that
## share one tire.
##
## Two separate bars would say the kart is doing 1.5 g of braking and 1.0 g of
## cornering. The circle says it is asking 1.8 g of a tire that has about 2 —
## which is the thing a driver can act on, and it is the same construction
## `tire.h`'s friction ellipse already models, so the instrument and the model
## agree by shape and not only by number.
##
## ## Sign convention, stated because getting it backwards is invisible
##
## `vehicle.h` computes `lateral_g = acceleration . basis_x / G` and `basis_x` is
## the chassis' **right**, so a positive lateral g is an acceleration to the right
## — a **right** turn — and the dot moves right. `longitudinal_g` is
## `-acceleration . basis_z / G` with `basis_z` rearward, so positive is forward
## acceleration and the dot moves up.
##
## That matters for one drawn detail. `chassis.h`'s rollover threshold is 2.4336 g
## **turning left** against 2.8061 turning right, because 27 kg of engine, exhaust
## and radiator hang off the right and put the center of mass 41 mm right of the
## centerline. The lower threshold therefore belongs on the **negative** lateral
## side. Drawing them symmetrically, or swapped, would show a driver a limit the
## kart does not have.
func _draw_g_meter(center: Vector2, radius: float) -> void:
	var per := radius / G_METER_MAX

	draw_circle(center, radius + 10.0 * _s, Color(0.024, 0.035, 0.051, 0.62))
	draw_arc(center, radius + 10.0 * _s, 0.0, TAU, 72, Color(1, 1, 1, 0.14), 1.0 * _s)

	# The sustained band as an annulus, drawn as a thick faint arc.
	draw_arc(center, (_sustained_min + _sustained_max) * 0.5 * per, 0.0, TAU, 72,
		Color(1, 1, 1, 0.10), (_sustained_max - _sustained_min) * per)

	for g: float in [1.0, 2.0]:
		draw_arc(center, g * per, 0.0, TAU, 72, Color(1, 1, 1, 0.26), 1.0 * _s)
	draw_line(center - Vector2(radius, 0.0), center + Vector2(radius, 0.0), Color(1, 1, 1, 0.18), 1.0 * _s)
	draw_line(center - Vector2(0.0, radius), center + Vector2(0.0, radius), Color(1, 1, 1, 0.18), 1.0 * _s)

	_draw_tip_mark(center, per, -_rollover_left, "TIP L")
	_draw_tip_mark(center, per, _rollover_right, "TIP R")

	# The trail, oldest to newest, fading in. Drawn under the dot so the dot sits
	# on top of its own history.
	var previous := Vector2.ZERO
	var has_previous := false
	for i in _trail_filled:
		var index := (_trail_head - _trail_filled + i + G_TRAIL_SAMPLES * 2) % G_TRAIL_SAMPLES
		var sample := _trail[index]
		var point := center + Vector2(sample.x, -sample.y) * per
		if has_previous:
			var age := float(i) / float(maxi(_trail_filled - 1, 1))
			draw_line(previous, point, Color(1.0, 0.72, 0.12, 0.08 + 0.62 * age), 2.4 * _s)
		previous = point
		has_previous = true

	var now := Vector2(kart.get_lateral_g(), kart.get_longitudinal_g())
	var dot := center + Vector2(now.x, -now.y) * per
	draw_circle(dot, 8.0 * _s, Color(0, 0, 0, 0.55))
	draw_circle(dot, 6.0 * _s, Color.WHITE)

	_text(center + Vector2(0.0, -radius - 22.0 * _s), "G", 13.0, Color(1, 1, 1, 0.42),
		HORIZONTAL_ALIGNMENT_CENTER, 0.9, 2.6)


func _draw_tip_mark(center: Vector2, per: float, at_g: float, label: String) -> void:
	if at_g == 0.0:
		return
	var x := center.x + at_g * per
	draw_line(Vector2(x, center.y - 11.0 * _s), Vector2(x, center.y + 11.0 * _s),
		Color(1, 1, 1, 0.85), 2.0 * _s)
	_text(Vector2(x, center.y + 28.0 * _s), label, 13.0, Color(1, 1, 1, 0.42),
		HORIZONTAL_ALIGNMENT_CENTER, 0.9, 1.4)


# --- primitives -------------------------------------------------------------


## A seven-segment digit, drawn as real segments.
##
## Drawn rather than typed on purpose. Godot's fallback font is a proportional
## humanist sans, which is not what an LCD looks like, and shipping a segment
## typeface would be an asset for ten glyphs. Drawing them also means the figure
## cannot silently change shape if the project theme ever gains a font override —
## which is the same class of problem as a texture that stops loading.
##
##     --a--
##    |f   b|
##     --g--
##    |e   c|
##     --d--
const _SEGMENTS := {
	"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc",
	"5": "afgcd", "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcfgd",
	"-": "g", " ": "",
}


func _seven(at: Vector2, w: float, h: float, glyph: String, on: Color, off: Color) -> void:
	var t := maxf(2.0, w * 0.17)
	var half := h * 0.5
	var lit: String = _SEGMENTS.get(glyph, "")

	_segment(lit.contains("a"), at, w, t, true, on, off)
	_segment(lit.contains("g"), at + Vector2(0.0, half - t * 0.5), w, t, true, on, off)
	_segment(lit.contains("d"), at + Vector2(0.0, h - t), w, t, true, on, off)
	_segment(lit.contains("f"), at, half, t, false, on, off)
	_segment(lit.contains("b"), at + Vector2(w - t, 0.0), half, t, false, on, off)
	_segment(lit.contains("e"), at + Vector2(0.0, half - t * 0.5), half, t, false, on, off)
	_segment(lit.contains("c"), at + Vector2(w - t, half - t * 0.5), half, t, false, on, off)


## One segment, as a hexagon with mitered ends — which is what makes a run of them
## read as a display rather than as a bar chart.
func _segment(lit: bool, at: Vector2, length: float, thickness: float, horizontal: bool,
		on: Color, off: Color) -> void:
	var points := PackedVector2Array()
	var t := thickness
	if horizontal:
		points.push_back(at + Vector2(t * 0.5, 0.0))
		points.push_back(at + Vector2(length - t * 0.5, 0.0))
		points.push_back(at + Vector2(length, t * 0.5))
		points.push_back(at + Vector2(length - t * 0.5, t))
		points.push_back(at + Vector2(t * 0.5, t))
		points.push_back(at + Vector2(0.0, t * 0.5))
	else:
		points.push_back(at + Vector2(0.0, t * 0.5))
		points.push_back(at + Vector2(t * 0.5, 0.0))
		points.push_back(at + Vector2(t, t * 0.5))
		points.push_back(at + Vector2(t, length - t * 0.5))
		points.push_back(at + Vector2(t * 0.5, length))
		points.push_back(at + Vector2(0.0, length - t * 0.5))
	draw_colored_polygon(points, on if lit else off)


## A string of seven-segment digits. Returns the width drawn.
##
## Three glyphs are not digits and none of them is a digit's width. A decimal
## point is a dot, a minus is one bar, and a space is a gap — giving each of them
## a full digit cell is what made "-1.34" read as a floating bar an inch from its
## number, and what put a digit-wide hole in front of a two-digit speed.
func _cell_width(glyph: String, w: float) -> float:
	match glyph:
		".": return w * 0.32
		"-": return w * 0.55
		" ": return w * 0.40
		_: return w


func _seven_string(at: Vector2, w: float, h: float, gap: float, value: String,
		on: Color, off: Color, align: int) -> float:
	var total := -gap
	for glyph in value:
		total += _cell_width(glyph, w) + gap

	var x := at.x
	if align == HORIZONTAL_ALIGNMENT_CENTER:
		x -= total * 0.5
	elif align == HORIZONTAL_ALIGNMENT_RIGHT:
		x -= total

	for glyph in value:
		var cell := _cell_width(glyph, w)
		match glyph:
			".":
				var d := maxf(3.0, w * 0.17)
				draw_rect(Rect2(Vector2(x, at.y + h - d), Vector2(d, d)), on, true)
			"-":
				# The `g` segment alone, at the cell's own width rather than a
				# digit's, so a minus sits against its number instead of beside it.
				var t := maxf(2.0, w * 0.17)
				_segment(true, Vector2(x, at.y + h * 0.5 - t * 0.5), cell, t, true, on, off)
			" ":
				pass
			_:
				_seven(Vector2(x, at.y), w, h, glyph, on, off)
		x += cell + gap
	return total


## Text, condensed by a horizontal transform and optionally tracked.
##
## There is no font asset here, so "condensed" is done with a scale rather than
## with a second typeface. That is font-independent, which matters because the
## fallback font is whatever Godot ships and this file must not depend on which
## one that is.
##
## Tracking is applied glyph by glyph because `draw_string` has no letter-spacing
## parameter. Only the small caps labels use it, so the per-glyph loop runs over a
## handful of characters.
func _text(at: Vector2, value: String, size: float, color: Color, align: int,
		stretch := 1.0, tracking := 0.0) -> void:
	var px := int(size * _s)
	var width := _font.get_string_size(value, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px).x * stretch
	if tracking != 0.0 and value.length() > 1:
		width += tracking * _s * (value.length() - 1)

	var origin := at
	if align == HORIZONTAL_ALIGNMENT_CENTER:
		origin.x -= width * 0.5
	elif align == HORIZONTAL_ALIGNMENT_RIGHT:
		origin.x -= width

	draw_set_transform(origin, 0.0, Vector2(stretch, 1.0))
	if tracking == 0.0:
		draw_string(_font, Vector2.ZERO, value, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px, color)
	else:
		var x := 0.0
		for glyph in value:
			draw_string(_font, Vector2(x, 0.0), glyph, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px, color)
			x += _font.get_string_size(glyph, HORIZONTAL_ALIGNMENT_LEFT, -1.0, px).x + tracking * _s / stretch
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
