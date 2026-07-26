class_name TelemetryPanel
extends CanvasLayer

## Issue #43's telemetry overlay: every quantity the M3b vehicle model computes,
## as numbers and as scrolling graphs. Toggled with `debug_telemetry` (F3).
##
## **This ships with M3b, not after it.** `ARCHITECTURE.md` §19 names unbounded
## vehicle tuning as a risk and names exactly one defense against it — telemetry
## landing with the vehicle. A tuning question answered by feel is the risk
## landing; the same question answered off a graph is not.
##
## ## Adding it to a scene
##
##     add_child(preload("res://scenes/ui/telemetry.tscn").instantiate())
##
## That is the whole integration. The panel then finds its data one of two ways:
##
##   * `set_source(callable)`, where `callable` returns the Dictionary below; or
##   * failing that, the first node in the `telemetry_source` group that has a
##     `telemetry()` method, looked up once and re-looked-up if it goes away.
##
## ## Running it without a kart
##
##     godot --path . scenes/ui/telemetry.tscn -- --telemetry=demo
##     godot --path . scenes/ui/telemetry.tscn -- --telemetry=demo \
##         --shot=/tmp/telemetry.png --shot-frame=240
##
## `--telemetry=demo` drives the panel from `_demo_sample()` instead of from a
## vehicle. That is not a workaround for the vehicle not existing yet — it is how
## a display gets tested at all. Every failure mode worth seeing (a wheel in the
## air, a shift, an over-rev, a frame-rate collapse) is a rare event on a real
## kart and a scheduled one in the generator, and `_demo_sample()` doubles as the
## executable statement of the Dictionary contract below.
##
## ## The Dictionary it consumes
##
## Keys are exactly the field names of `kart::core::VehicleTelemetry`, and
## `wheel` is an Array of four Dictionaries keyed by `WheelTelemetry`'s field
## names, in `CORNER_COUNT` order — FL, FR, RL, RR.
##
##     {
##         "wheel": [ {                     # x4, FL FR RL RR
##             "normal_load": float,        # N
##             "slip_angle": float,         # rad
##             "slip_ratio": float,         # dimensionless
##             "suspension_travel": float,  # m, positive compressed
##             "lift": float,               # m off the ground
##             "utilization": float,        # fraction of the friction ellipse
##             "steer_angle": float,        # rad
##             "force": Vector3,            # world, N
##             "grounded": bool,
##         } ],
##         "engine_rpm": float, "engine_torque": float, "axle_torque": float,
##         "axle_speed": float, "gear": int, "clutch_slip": float,
##         "clutch_torque": float, "shifting": bool, "over_rev": bool,
##         "speed_ms": float, "lateral_g": float, "longitudinal_g": float,
##         "frame_warp": float, "substeps": int, "time_ratio": float,
##     }
##
## Every key is read with a default, so a publisher that is missing one draws a
## zero rather than crashing. An empty Dictionary means "no vehicle yet" and the
## panel says so once instead of spamming.
##
## ## It never writes
##
## The only thing this file calls on anything outside itself is the source
## Callable. `ARCHITECTURE.md` §8 requires a replay to be identical whether or not
## the panel was open, and the cheapest way to guarantee that is for the panel to
## have no way to say anything back.
##
## ## What it costs, measured
##
## `--telemetry-profile` prints both halves every two seconds. On an M1 Pro at
## 1920x1080 with all twelve graphs live:
##
##     sample  20.6 us/tick   2.5 ms per wall-clock second
##     draw     252 us/redraw  78 ms per wall-clock second
##
## which is 0.65 ms per frame at 120 Hz and 1.3 ms at 60 Hz. That is the whole of
## §15's game-logic-and-UI budget at 60 Hz, and it is spent by something that is
## off by default and exists to be looked at — but it is not free and the flag is
## there so the next person does not have to take this comment's word for it.
##
## Three decisions got it there from the 9.6 ms a redraw the first version cost:
##
##   * **Sampling is separate from drawing.** 32 channels are written into
##     preallocated `PackedFloat32Array` ring buffers in `_physics_process`, one
##     float each, no allocation. Sampling continues while the panel is hidden,
##     so pressing F3 after something surprising shows the four seconds that led
##     to it.
##   * **Every graph is its own canvas item, redrawn on a rota.** A canvas item
##     can only be re-recorded whole, so one item for the whole panel meant one
##     3.1 ms stutter every redraw. Thirteen items redrawn a few per frame cost
##     the same per second and a tenth as much in any one frame.
##   * **Traces are decimated and the scratch array is reused.** One point per
##     six pixels of plot width — a trace is a shape, not a table — into a
##     `PackedVector2Array` sized at layout time, so a redraw allocates nothing.
##
## ## Comfort and accessibility
##
## `ARCHITECTURE.md` §18. The four wheels are never distinguished by color alone:
## each trace carries a distinct marker shape at its live end plus its two-letter
## code as text, the legend repeats both, and every number is in a labeled column.
## The palette is Okabe-Ito, which is the standard colorblind-safe eight.

# --- the contract ------------------------------------------------------------

## Wheel order, `CORNER_COUNT` order, from `chassis_flex.h`: front left, front
## right, rear left, rear right. `MODE_ROLL` there is {1,-1,1,-1}, which is what
## fixes this ordering rather than convention.
const WHEEL_CODES: PackedStringArray = ["FL", "FR", "RL", "RR"]
const WHEEL_COUNT := 4

# --- history -----------------------------------------------------------------

## Samples kept per channel. 512 at the 120 Hz physics rate is a 4.27 s window,
## which is about one corner: long enough to hold an entry, an apex and an exit
## in one frame, short enough that a spike is still visibly a spike.
const HISTORY := 512

## Channel ids. One flat numbering so the ring buffer is a single array of arrays
## and a graph is just a list of channel ids.
const CH_LOAD := 0 # +4, one per wheel
const CH_SLIP_ANGLE := 4 # +4
const CH_SLIP_RATIO := 8 # +4
const CH_UTILIZATION := 12 # +4
const CH_LIFT := 16 # +4
const CH_TRAVEL := 20 # +4
const CH_RPM := 24
const CH_LATERAL_G := 25
const CH_LONGITUDINAL_G := 26
const CH_TIME_RATIO := 27
const CH_CLUTCH_SLIP := 28
const CH_ENGINE_TORQUE := 29
const CH_AXLE_TORQUE := 30
const CH_FRAME_WARP := 31
const CHANNEL_COUNT := 32

# --- look --------------------------------------------------------------------

## Okabe-Ito, the colorblind-safe eight. Wheel order FL, FR, RL, RR.
const WHEEL_COLORS: PackedColorArray = [
	Color(0.902, 0.624, 0.000), # E69F00 orange
	Color(0.337, 0.706, 0.914), # 56B4E9 sky blue
	Color(0.000, 0.620, 0.451), # 009E73 bluish green
	Color(0.800, 0.475, 0.655), # CC79A7 reddish purple
]

## The non-color half of §18: a distinct marker at each trace's live end.
const MARK_SQUARE := 0
const MARK_TRIANGLE := 1
const MARK_CIRCLE := 2
const MARK_DIAMOND := 3

const COLOR_BACKGROUND := Color(0.04, 0.05, 0.07, 0.62)
const COLOR_STRIP := Color(0.04, 0.05, 0.07, 0.88)
const COLOR_PANEL := Color(0.09, 0.10, 0.13, 0.72)
const COLOR_GRID := Color(1.0, 1.0, 1.0, 0.09)
const COLOR_ZERO := Color(1.0, 1.0, 1.0, 0.22)
const COLOR_TEXT := Color(0.88, 0.90, 0.94)
const COLOR_DIM := Color(0.60, 0.63, 0.70)
const COLOR_ALERT := Color(0.835, 0.369, 0.000) # D55E00, Okabe-Ito vermillion
const COLOR_GOOD := Color(0.000, 0.620, 0.451) # 009E73
const COLOR_CHASSIS := Color(0.941, 0.894, 0.259) # F0E442, Okabe-Ito yellow
const COLOR_SECOND := Color(0.000, 0.447, 0.698) # 0072B2, Okabe-Ito blue

const FONT_SIZE_SMALL := 12
const FONT_SIZE_BODY := 14
const FONT_SIZE_TITLE := 16
const FONT_SIZE_HUGE := 34

## Height of the always-on status strip across the top, pixels. The strip holds
## the two numbers that change the meaning of every other number on the panel —
## `time_ratio` and wheel lift — so it is the one part that is never scrolled off
## or squeezed.
const STRIP_HEIGHT := 92.0

## Where the lower block starts, as a fraction of viewport height. The graphs sit
## in the bottom of the frame so that the road ahead stays visible with the panel
## open; a tuning overlay that has to be closed to drive gets closed.
const LOWER_TOP_FRACTION := 0.44

const TABLE_HEIGHT := 168.0
const GRAPH_COLUMNS := 4
const GRAPH_ROWS := 3
const PADDING := 10.0

## One redraw per 1/60 s. Sampling stays at the 120 Hz physics rate — this only
## limits how often the same four-second window is re-rasterized, and 60 Hz is
## twice the rate at which a scrolling trace stops looking continuous.
const REDRAW_INTERVAL := 1.0 / 24.0

## `time_ratio` outside 1 +/- this is called out. ADR-0033 finding 7 measured
## 0.6476 under a frame-rate collapse; 2% is well inside the noise of a healthy
## frame and well outside anything that matters.
const TIME_RATIO_TOLERANCE := 0.02

## Lift below this reads as raycast noise rather than as a wheel in the air.
const LIFT_VISIBLE_M := 0.001

# --- state -------------------------------------------------------------------

var _canvas: Control

## The canvas item currently being drawn into. Set by the two draw entry points
## and read by every helper below, so that the helpers do not each have to take
## a target they would never get wrong in more than one way.
var _target: CanvasItem
var _font: Font
var _source: Callable
var _sample: Dictionary = {}
var _has_sample := false
var _warned_no_source := false
var _search_countdown := 0

var _graph_specs: Array[Dictionary] = []
## Flattened out of `_graph_specs` in `_ready`, because the span update runs at
## the physics rate and walking a list of Dictionaries there would allocate.
var _span_channels: Array[PackedInt32Array] = []
var _span_defaults := PackedFloat32Array()

var _history: Array[PackedFloat32Array] = []
var _head := 0
var _filled := 0
var _spans := PackedFloat32Array()


## `--telemetry-profile` prints what the panel costs, every two seconds, in the
## two places it can cost anything. ARCHITECTURE.md §15 budgets 1.0 ms for game
## logic and UI together, and a debug overlay that quietly eats it would be
## discovered as "the sim got slower" rather than as this.
var _profile := false
var _push_usec := 0
var _draw_usec := 0
var _push_count := 0
var _draw_count := 0

var _demo := false
var _demo_tick := 0
var _shot_path := ""
var _shot_frame := -1
var _frames := 0

## Peak lift seen in the current window, per wheel, meters. Held rather than
## instantaneous because the moment a wheel touches down again is exactly when a
## driver looks at the panel.
var _peak_lift := PackedFloat32Array()

## Reused every redraw so that drawing allocates nothing. Sized in `_layout`.
## Keyed by "size|text". Only ever holds label constants — a value that changes
## every frame is laid out against a fixed column instead, so this cannot grow.
var _width_cache: Dictionary = {}

var _points := PackedVector2Array()
var _plot_points := 0

var _strip_rect := Rect2()
var _table_rect := Rect2()
var _graph_canvases: Array[Control] = []

## Which target redraws next: 0 is the strip and the wheel table, 1..12 are the
## graphs. `_redraw_credit` is how many targets are owed a redraw, carried across
## frames so the schedule does not drift with the frame rate.
var _redraw_cursor := 0
var _redraw_credit := 0.0


func _ready() -> void:
	layer = 100

	_history.resize(CHANNEL_COUNT)
	for channel in CHANNEL_COUNT:
		var buffer := PackedFloat32Array()
		buffer.resize(HISTORY)
		_history[channel] = buffer
	_graph_specs = _build_graphs()
	_spans.resize(_graph_specs.size())
	_span_defaults.resize(_graph_specs.size())
	for index in _graph_specs.size():
		var spec: Dictionary = _graph_specs[index]
		_span_defaults[index] = spec["span"]
		var channels := PackedInt32Array()
		for channel: int in spec["channels"]:
			channels.append(channel)
		_span_channels.append(channels)
	_peak_lift.resize(WHEEL_COUNT)

	_font = ThemeDB.fallback_font

	_canvas = Control.new()
	_canvas.name = "Canvas"
	_canvas.set_anchors_preset(Control.PRESET_FULL_RECT)
	# Never eats a click. The panel is an overlay on a scene that is being driven.
	_canvas.mouse_filter = Control.MOUSE_FILTER_IGNORE
	# `_draw` needs a script on a CanvasItem; the `draw` signal is the same hook
	# without a second file, and it keeps the whole panel in one place.
	_canvas.draw.connect(_on_canvas_draw)
	_canvas.resized.connect(_layout)
	_canvas.visible = false
	add_child(_canvas)

	# One CanvasItem per graph rather than one for the whole panel, which is what
	# makes the redraw schedule below possible: a canvas item can only be
	# re-recorded whole, so thirteen of them can be re-recorded a few at a time
	# while one of them could only ever be re-recorded all at once. Children of
	# `_canvas`, so hiding the panel hides them and stops their drawing entirely.
	for index in _graph_specs.size():
		var cell := Control.new()
		cell.name = "Graph%02d" % index
		cell.mouse_filter = Control.MOUSE_FILTER_IGNORE
		cell.draw.connect(_on_graph_draw.bind(index))
		_canvas.add_child(cell)
		_graph_canvases.append(cell)

	var args := Cmdline.parse()
	_demo = Cmdline.as_string(args, "telemetry", "") == "demo"
	_profile = Cmdline.as_bool(args, "telemetry-profile", false)
	_shot_path = Cmdline.as_string(args, "shot", "")
	_shot_frame = Cmdline.as_int(args, "shot-frame", 180)
	if _demo:
		_canvas.visible = true

	_layout()


## Point the panel at a publisher. The Callable takes nothing and returns the
## Dictionary this file's header documents; an empty Dictionary means the vehicle
## is not up yet, which is not an error.
func set_source(source: Callable) -> void:
	_source = source
	_warned_no_source = false


func is_open() -> bool:
	return _canvas != null and _canvas.visible


# --- run time ----------------------------------------------------------------


func _physics_process(_delta: float) -> void:
	# Sampling runs whether the panel is visible or not: F3 is usually pressed
	# just after the interesting thing happened, and a window that starts empty
	# at that moment is a window that missed it.
	_sample = _read_source()
	_has_sample = not _sample.is_empty()
	if not _has_sample:
		return
	if not _profile:
		_push(_sample)
		return
	var started := Time.get_ticks_usec()
	_push(_sample)
	_push_usec += Time.get_ticks_usec() - started
	_push_count += 1
	if _push_count >= 240:
		# Reported per wall-clock second as well as per redraw, because the per
		# redraw figure says nothing about the budget on its own: what §15 cares
		# about is the fraction of a frame this takes, and that is the total.
		var window := float(_push_count) / float(Engine.physics_ticks_per_second)
		print(("telemetry: sample %.1f us/tick, %.2f ms/s | draw %.1f us/redraw, "
				+ "%.2f ms/s over %d redraws | %.0f fps")
				% [float(_push_usec) / float(_push_count),
				float(_push_usec) / 1000.0 / window,
				(float(_draw_usec) / float(_draw_count)) if _draw_count > 0 else 0.0,
				float(_draw_usec) / 1000.0 / window, _draw_count,
				Engine.get_frames_per_second()])
		_push_usec = 0
		_push_count = 0
		_draw_usec = 0
		_draw_count = 0


func _process(delta: float) -> void:
	if Input.is_action_just_pressed(&"debug_telemetry"):
		_canvas.visible = not _canvas.visible

	if _canvas.visible:
		_schedule_redraws(delta)

	_frames += 1
	if _shot_path == "":
		return
	if _frames == _shot_frame - 1:
		# Everything at once for the frame a still is taken on, since a still that
		# caught a third of the panel mid-schedule would not be reproducible.
		_canvas.queue_redraw()
		for cell in _graph_canvases:
			cell.queue_redraw()
	elif _frames == _shot_frame:
		_save_shot()


## Redraw a few targets per frame instead of all thirteen at once.
##
## The measured cost of re-recording the whole panel is 3.1 ms, which is three
## times ARCHITECTURE.md §15's entire budget for game logic and UI — and it lands
## in one frame, so it is a stutter rather than a tax. Thirteen canvas items
## re-recorded a few at a time cost the same per second and roughly a tenth as
## much in any one frame.
##
## The rate is time-based rather than a fixed count, so every target is redrawn
## at least once per `REDRAW_INTERVAL` whatever the frame rate is. When the frame
## rate collapses the whole panel refreshes in one frame — which is the right way
## round, because that is exactly when `time_ratio` has something to say.
func _schedule_redraws(delta: float) -> void:
	var targets := _graph_canvases.size() + 1
	_redraw_credit += delta / REDRAW_INTERVAL * float(targets)
	var due := mini(int(_redraw_credit), targets)
	_redraw_credit -= float(due)
	for _step in due:
		if _redraw_cursor == 0:
			_canvas.queue_redraw()
		else:
			_graph_canvases[_redraw_cursor - 1].queue_redraw()
		_redraw_cursor = (_redraw_cursor + 1) % targets


func _save_shot() -> void:
	# A still of the panel, so that a layout change can be judged from a command
	# rather than from a description of it. Never compared by hash — ADR-0023.
	var image := get_viewport().get_texture().get_image()
	var error := image.save_png(_shot_path)
	if error != OK:
		push_error("telemetry: could not write %s (error %d)" % [_shot_path, error])
	else:
		print("telemetry: wrote ", _shot_path)
	get_tree().quit()


func _read_source() -> Dictionary:
	if _demo:
		_demo_tick += 1
		return _demo_sample(_demo_tick)

	if not _source.is_valid():
		# Looked for twice a second rather than every tick: the group query
		# allocates, and a vehicle that is not there yet will not be there a
		# hundredth of a second later either.
		_search_countdown -= 1
		if _search_countdown <= 0:
			_search_countdown = 60
			_find_source()
	if not _source.is_valid():
		if not _warned_no_source:
			_warned_no_source = true
			print("telemetry: no source; call set_source() or join the ",
					"telemetry_source group. Run with --telemetry=demo to exercise the panel.")
		return {}

	var produced: Variant = _source.call()
	return produced if produced is Dictionary else {}


## The zero-integration path: anything in the `telemetry_source` group that can
## answer `telemetry()`. Looked up only while there is no source, so the common
## case costs nothing.
func _find_source() -> void:
	for node in get_tree().get_nodes_in_group(&"telemetry_source"):
		if node.has_method(&"telemetry"):
			_source = Callable(node, &"telemetry")
			_warned_no_source = false
			return


# --- history -----------------------------------------------------------------


func _push(sample: Dictionary) -> void:
	_head = (_head + 1) % HISTORY
	if _filled < HISTORY:
		_filled += 1

	var wheels: Array = sample.get("wheel", [])
	for index in WHEEL_COUNT:
		var wheel: Dictionary = wheels[index] if index < wheels.size() else {}
		var lift := float(wheel.get("lift", 0.0))
		_write(CH_LOAD + index, float(wheel.get("normal_load", 0.0)))
		_write(CH_SLIP_ANGLE + index, rad_to_deg(float(wheel.get("slip_angle", 0.0))))
		_write(CH_SLIP_RATIO + index, float(wheel.get("slip_ratio", 0.0)))
		_write(CH_UTILIZATION + index, float(wheel.get("utilization", 0.0)))
		_write(CH_LIFT + index, lift * 1000.0)
		_write(CH_TRAVEL + index, float(wheel.get("suspension_travel", 0.0)) * 1000.0)
		if lift > _peak_lift[index]:
			_peak_lift[index] = lift
		else:
			# Decays over roughly the length of the history window, so the peak
			# readout describes what is on screen rather than what happened once
			# five minutes ago.
			_peak_lift[index] *= 0.995

	_write(CH_RPM, float(sample.get("engine_rpm", 0.0)))
	_write(CH_LATERAL_G, float(sample.get("lateral_g", 0.0)))
	_write(CH_LONGITUDINAL_G, float(sample.get("longitudinal_g", 0.0)))
	_write(CH_TIME_RATIO, float(sample.get("time_ratio", 1.0)))
	_write(CH_CLUTCH_SLIP, float(sample.get("clutch_slip", 0.0)))
	_write(CH_ENGINE_TORQUE, float(sample.get("engine_torque", 0.0)))
	_write(CH_AXLE_TORQUE, float(sample.get("axle_torque", 0.0)))
	_write(CH_FRAME_WARP, float(sample.get("frame_warp", 0.0)) * 1000.0)

	_update_spans()


func _write(channel: int, value: float) -> void:
	_history[channel][_head] = value


## Vertical scale per graph: the larger of the graph's own sensible default and
## what the window actually contains, with a slow decay back so that one spike
## does not squash the trace forever. Updated once per sample rather than per
## redraw, because a scale that changes at the drawing rate flickers.
func _update_spans() -> void:
	for index in _span_channels.size():
		var peak := 0.0
		for channel: int in _span_channels[index]:
			peak = maxf(peak, absf(_history[channel][_head]))
		_spans[index] = maxf(_spans[index] * 0.998,
				maxf(_span_defaults[index], peak * 1.15))


# --- layout ------------------------------------------------------------------


func _layout() -> void:
	if _canvas == null:
		return
	var size := _canvas.size
	if size.x < 1.0 or size.y < 1.0:
		return

	_strip_rect = Rect2(0.0, 0.0, size.x, STRIP_HEIGHT)

	var lower_top := maxf(STRIP_HEIGHT + PADDING, size.y * LOWER_TOP_FRACTION)
	_table_rect = Rect2(PADDING, lower_top, size.x - PADDING * 2.0, TABLE_HEIGHT)

	var grid_top := _table_rect.end.y + PADDING
	var grid_height := size.y - grid_top - PADDING
	var cell_width := (size.x - PADDING * float(GRAPH_COLUMNS + 1)) / float(GRAPH_COLUMNS)
	var cell_height := (grid_height - PADDING * float(GRAPH_ROWS - 1)) / float(GRAPH_ROWS)

	for index in _graph_canvases.size():
		var column := index % GRAPH_COLUMNS
		@warning_ignore("integer_division")
		var row := index / GRAPH_COLUMNS
		var cell: Control = _graph_canvases[index]
		cell.position = Vector2(
			PADDING + float(column) * (cell_width + PADDING),
			grid_top + float(row) * (cell_height + PADDING)
		)
		cell.size = Vector2(cell_width, cell_height)

	# One point per four pixels of plot width. A trace is a shape, not a table: at
	# four pixels a corner still reads as a corner, and the redraw cost is linear
	# in this number. Measured at one point per pixel it was 9.6 ms a redraw,
	# which is nine times ARCHITECTURE.md §15's entire UI budget.
	_plot_points = clampi(int(cell_width / 6.0), 8, HISTORY)
	_points.resize(_plot_points)


## The graph grid, in reading order. `channels` are ids into the ring buffer,
## `span` is the default half-height in the graph's own unit, `bipolar` puts zero
## in the middle, `series` is what the legend says.
func _build_graphs() -> Array[Dictionary]:
	return [
		{
			"title": "WHEEL LIFT", "unit": "mm", "span": 10.0, "bipolar": false,
			"channels": [CH_LIFT, CH_LIFT + 1, CH_LIFT + 2, CH_LIFT + 3],
			"series": WHEEL_CODES, "wheels": true, "alert": true, "width": 2.6,
		},
		{
			"title": "NORMAL LOAD", "unit": "N", "span": 700.0, "bipolar": false,
			"channels": [CH_LOAD, CH_LOAD + 1, CH_LOAD + 2, CH_LOAD + 3],
			"series": WHEEL_CODES, "wheels": true, "alert": false, "width": 1.6,
		},
		{
			"title": "UTILIZATION", "unit": "", "span": 1.2, "bipolar": false,
			"channels": [CH_UTILIZATION, CH_UTILIZATION + 1, CH_UTILIZATION + 2, CH_UTILIZATION + 3],
			"series": WHEEL_CODES, "wheels": true, "alert": false, "width": 1.6,
		},
		{
			"title": "SLIP ANGLE", "unit": "deg", "span": 12.0, "bipolar": true,
			"channels": [CH_SLIP_ANGLE, CH_SLIP_ANGLE + 1, CH_SLIP_ANGLE + 2, CH_SLIP_ANGLE + 3],
			"series": WHEEL_CODES, "wheels": true, "alert": false, "width": 1.6,
		},
		{
			"title": "SLIP RATIO", "unit": "", "span": 0.3, "bipolar": true,
			"channels": [CH_SLIP_RATIO, CH_SLIP_RATIO + 1, CH_SLIP_RATIO + 2, CH_SLIP_RATIO + 3],
			"series": WHEEL_CODES, "wheels": true, "alert": false, "width": 1.6,
		},
		{
			"title": "SUSPENSION TRAVEL", "unit": "mm", "span": 6.0, "bipolar": true,
			"channels": [CH_TRAVEL, CH_TRAVEL + 1, CH_TRAVEL + 2, CH_TRAVEL + 3],
			"series": WHEEL_CODES, "wheels": true, "alert": false, "width": 1.6,
		},
		{
			"title": "FRAME WARP", "unit": "mm", "span": 4.0, "bipolar": true,
			"channels": [CH_FRAME_WARP], "series": PackedStringArray(["warp"]),
			"wheels": false, "alert": false, "width": 1.6,
		},
		{
			"title": "CHASSIS g", "unit": "g", "span": 2.5, "bipolar": true,
			"channels": [CH_LATERAL_G, CH_LONGITUDINAL_G],
			"series": PackedStringArray(["lat", "long"]),
			"wheels": false, "alert": false, "width": 1.6,
		},
		{
			"title": "ENGINE RPM", "unit": "rpm", "span": 14500.0, "bipolar": false,
			"channels": [CH_RPM], "series": PackedStringArray(["rpm"]),
			"wheels": false, "alert": false, "width": 1.6,
		},
		{
			"title": "TORQUE", "unit": "N m", "span": 40.0, "bipolar": true,
			"channels": [CH_ENGINE_TORQUE, CH_AXLE_TORQUE],
			"series": PackedStringArray(["engine", "axle"]),
			"wheels": false, "alert": false, "width": 1.6,
		},
		{
			"title": "CLUTCH SLIP", "unit": "rad/s", "span": 100.0, "bipolar": false,
			"channels": [CH_CLUTCH_SLIP], "series": PackedStringArray(["slip"]),
			"wheels": false, "alert": false, "width": 1.6,
		},
		{
			"title": "TIME RATIO", "unit": "sim s / real s", "span": 1.2, "bipolar": false,
			"channels": [CH_TIME_RATIO], "series": PackedStringArray(["ratio"]),
			"wheels": false, "alert": true, "width": 2.6,
		},
	]


# --- drawing -----------------------------------------------------------------


func _on_canvas_draw() -> void:
	_target = _canvas
	if _profile:
		var started := Time.get_ticks_usec()
		_paint()
		_draw_usec += Time.get_ticks_usec() - started
		_draw_count += 1
		return
	_paint()


func _paint() -> void:
	var size := _canvas.size
	var lower_top := size.y * LOWER_TOP_FRACTION - PADDING
	_target.draw_rect(Rect2(Vector2(0.0, lower_top), Vector2(size.x, size.y - lower_top)),
			COLOR_BACKGROUND)

	_draw_strip()

	if not _has_sample:
		_text(Vector2(PADDING * 2.0, _table_rect.position.y + 30.0),
				"no telemetry source — run with --telemetry=demo, or call set_source()",
				FONT_SIZE_TITLE, COLOR_DIM)
		return

	_draw_wheel_table()


## The status strip. Two things on it change what every other number means, so
## they get the space: `time_ratio` and wheel lift.
func _draw_strip() -> void:
	_target.draw_rect(_strip_rect, COLOR_STRIP)

	var ratio := float(_sample.get("time_ratio", 1.0)) if _has_sample else 1.0
	var slow := absf(ratio - 1.0) > TIME_RATIO_TOLERANCE
	if slow:
		# ADR-0033 finding 7: `max_physics_steps_per_frame` clamps and does not
		# bank, so under a frame-rate collapse the simulation falls behind real
		# time and never catches up. A tick-counting replay cannot see it. When
		# this is not 1.0, every other number on this panel describes a kart in
		# slow motion, so the whole panel is framed in the alert color rather
		# than the number being marked quietly.
		var edge := Rect2(Vector2.ZERO, _canvas.size)
		_target.draw_rect(edge, COLOR_ALERT, false, 4.0)
		_target.draw_rect(_strip_rect, Color(COLOR_ALERT.r, COLOR_ALERT.g, COLOR_ALERT.b, 0.22))

	var ratio_color := COLOR_ALERT if slow else COLOR_GOOD
	_text(Vector2(PADDING * 2.0, 42.0), "%.3f" % ratio, FONT_SIZE_HUGE, ratio_color)
	_text(Vector2(PADDING * 2.0, 62.0), "sim s / real s", FONT_SIZE_SMALL, COLOR_DIM)
	if slow:
		_text(Vector2(PADDING * 2.0, 82.0),
				"SIMULATION AT %.0f%% OF REAL TIME — every number below is slow motion"
						% (ratio * 100.0),
				FONT_SIZE_BODY, COLOR_ALERT)

	var x := 170.0
	x = _stat(x, "SPEED",
			"%.1f km/h" % (float(_sample.get("speed_ms", 0.0)) * 3.6), COLOR_TEXT, 100.0)
	x = _stat(x, "LAT g", "%+.2f" % float(_sample.get("lateral_g", 0.0)), COLOR_CHASSIS, 62.0)
	x = _stat(x, "LONG g",
			"%+.2f" % float(_sample.get("longitudinal_g", 0.0)), COLOR_CHASSIS, 70.0)
	x = _stat(x, "GEAR", str(int(_sample.get("gear", 0))), COLOR_TEXT, 56.0)
	x = _stat(x, "RPM", "%.0f" % float(_sample.get("engine_rpm", 0.0)),
			COLOR_ALERT if bool(_sample.get("over_rev", false)) else COLOR_TEXT, 66.0)
	x = _stat(x, "AXLE",
			"%.1f rad/s" % float(_sample.get("axle_speed", 0.0)), COLOR_TEXT, 106.0)
	x = _stat(x, "CLUTCH",
			"%.1f N m" % float(_sample.get("clutch_torque", 0.0)), COLOR_TEXT, 96.0)
	x = _stat(x, "SUBSTEPS", str(int(_sample.get("substeps", 0))), COLOR_TEXT, 78.0)
	x = _stat(x, "FPS", "%.0f" % Engine.get_frames_per_second(), COLOR_DIM, 62.0)

	if bool(_sample.get("shifting", false)):
		x = _stat(x, "", "SHIFTING", COLOR_SECOND, 92.0)
	if bool(_sample.get("over_rev", false)):
		x = _stat(x, "", "OVER-REV", COLOR_ALERT, 96.0)

	_draw_lift_banner(x)


## Issue #32's number, given the room it is owed. A kart's inside rear leaving
## the ground is the thing ARCHITECTURE.md §6 says the whole vehicle model exists
## to produce — with no differential, that wheel lift *is* the differential — so
## the panel says so in words, per wheel, in millimeters, the instant it happens.
func _draw_lift_banner(from_x: float) -> void:
	var wheels: Array = _sample.get("wheel", [])
	var airborne := PackedStringArray()
	var worst := 0.0
	for index in WHEEL_COUNT:
		if index >= wheels.size():
			continue
		var wheel: Dictionary = wheels[index]
		var lift := float(wheel.get("lift", 0.0))
		if lift > LIFT_VISIBLE_M:
			airborne.append("%s +%.0f mm" % [WHEEL_CODES[index], lift * 1000.0])
			worst = maxf(worst, lift)

	var x := maxf(from_x, _canvas.size.x - 430.0)
	if airborne.is_empty():
		_text(Vector2(x, 34.0), "all four down", FONT_SIZE_BODY, COLOR_DIM)
	else:
		var banner := Rect2(x - 8.0, 8.0, _canvas.size.x - x, STRIP_HEIGHT - 32.0)
		_target.draw_rect(banner, Color(COLOR_CHASSIS.r, COLOR_CHASSIS.g, COLOR_CHASSIS.b, 0.20))
		_target.draw_rect(banner, COLOR_CHASSIS, false, 2.0)
		_text(Vector2(x, 32.0), "WHEEL OFF THE GROUND", FONT_SIZE_TITLE, COLOR_CHASSIS)
		_text(Vector2(x, 54.0), " ".join(airborne), FONT_SIZE_BODY, COLOR_TEXT)

	# The peak holds after touchdown, because the moment a driver looks at the
	# panel is the moment after the kart came back down.
	var peaks := PackedStringArray()
	for index in WHEEL_COUNT:
		peaks.append("%s %.0f" % [WHEEL_CODES[index], _peak_lift[index] * 1000.0])
	_text(Vector2(x, 78.0), "peak lift, mm: " + "  ".join(peaks), FONT_SIZE_SMALL, COLOR_DIM)


## One labeled reading in the status strip, at a caller-given column width.
##
## The width is passed rather than measured because the value changes every
## frame, and measuring a changing string is a font shaping call per stat per
## redraw. A fixed column also stops the strip from jittering sideways as a
## number gains a digit, which is worse to read than a little wasted space.
func _stat(x: float, label: String, value: String, color: Color, width: float) -> float:
	if label != "":
		_text(Vector2(x, 26.0), label, FONT_SIZE_SMALL, COLOR_DIM)
	_text(Vector2(x, 48.0), value, FONT_SIZE_TITLE, color)
	return x + width


## Every per-wheel field issue #43 lists, as numbers, in four labeled columns.
## The graphs show shape over time; this shows the value now, to the digit, which
## is what a tuning change is checked against.
func _draw_wheel_table() -> void:
	_target.draw_rect(_table_rect, COLOR_PANEL)
	var wheels: Array = _sample.get("wheel", [])

	var rows := [
		"normal load N", "slip angle deg", "slip ratio", "utilization",
		"susp travel mm", "lift mm", "steer deg", "force N", "grounded",
	]
	var label_width := 128.0
	var column_width := (_table_rect.size.x - label_width - PADDING * 2.0) / float(WHEEL_COUNT)
	var top := _table_rect.position.y + 22.0
	var line_height := 15.0

	for row in rows.size():
		_text(Vector2(_table_rect.position.x + PADDING, top + float(row) * line_height),
				rows[row], FONT_SIZE_SMALL, COLOR_DIM)

	for index in WHEEL_COUNT:
		var column_x := _table_rect.position.x + PADDING + label_width \
				+ float(index) * column_width
		var color := WHEEL_COLORS[index]
		# The header carries the marker shape as well as the color, so the column
		# and its trace can be matched without seeing either hue. §18.
		_mark(Vector2(column_x + 6.0, top - 14.0), index, color, 5.0)
		_text(Vector2(column_x + 16.0, top - 10.0), WHEEL_CODES[index], FONT_SIZE_BODY, color)

		var wheel: Dictionary = wheels[index] if index < wheels.size() else {}
		var force: Vector3 = wheel.get("force", Vector3.ZERO)
		var grounded := bool(wheel.get("grounded", false))
		var values := PackedStringArray([
			"%.0f" % float(wheel.get("normal_load", 0.0)),
			"%+.2f" % rad_to_deg(float(wheel.get("slip_angle", 0.0))),
			"%+.3f" % float(wheel.get("slip_ratio", 0.0)),
			"%.3f" % float(wheel.get("utilization", 0.0)),
			"%+.2f" % (float(wheel.get("suspension_travel", 0.0)) * 1000.0),
			"%.1f" % (float(wheel.get("lift", 0.0)) * 1000.0),
			"%+.2f" % rad_to_deg(float(wheel.get("steer_angle", 0.0))),
			"%.0f  (%.0f, %.0f, %.0f)" % [force.length(), force.x, force.y, force.z],
			"yes" if grounded else "NO",
		])
		for row in values.size():
			var value_color := COLOR_TEXT
			if row == 3 and float(wheel.get("utilization", 0.0)) > 1.0:
				value_color = COLOR_ALERT # sliding
			if row == 5 and float(wheel.get("lift", 0.0)) > LIFT_VISIBLE_M:
				value_color = COLOR_CHASSIS
			if row == 8 and not grounded:
				value_color = COLOR_CHASSIS
			_text(Vector2(column_x + 16.0, top + float(row) * line_height),
					values[row], FONT_SIZE_SMALL, value_color)


func _on_graph_draw(index: int) -> void:
	if not _has_sample or index >= _graph_specs.size():
		return
	var target: Control = _graph_canvases[index]
	_target = target
	var rect := Rect2(Vector2.ZERO, target.size)
	if _profile:
		var started := Time.get_ticks_usec()
		_draw_graph(rect, _graph_specs[index], _spans[index])
		_draw_usec += Time.get_ticks_usec() - started
		_draw_count += 1
		return
	_draw_graph(rect, _graph_specs[index], _spans[index])


## One graph, in its own canvas item's local coordinates.
func _draw_graph(rect: Rect2, graph: Dictionary, span: float) -> void:
	_target.draw_rect(rect, COLOR_PANEL)
	if bool(graph["alert"]):
		_target.draw_rect(rect, Color(COLOR_CHASSIS.r, COLOR_CHASSIS.g, COLOR_CHASSIS.b, 0.35),
				false, 1.0)

	var title: String = graph["title"]
	var unit: String = graph["unit"]
	_text(rect.position + Vector2(6.0, 14.0), title, FONT_SIZE_BODY, COLOR_TEXT)
	if unit != "":
		_text(rect.position + Vector2(12.0 + _text_width(title, FONT_SIZE_BODY), 14.0),
				unit, FONT_SIZE_SMALL, COLOR_DIM)

	var plot := Rect2(
		rect.position.x + 4.0, rect.position.y + 20.0,
		rect.size.x - 8.0, rect.size.y - 26.0
	)
	if plot.size.x <= 2.0 or plot.size.y <= 2.0:
		return

	var bipolar := bool(graph["bipolar"])
	var top_value := span
	var bottom_value := -span if bipolar else 0.0

	# Gridline at the halves, and a heavier one at zero, so a trace can be read
	# against a value rather than only against its own shape.
	for fraction: float in [0.25, 0.5, 0.75]:
		var y := plot.position.y + plot.size.y * fraction
		_target.draw_line(Vector2(plot.position.x, y), Vector2(plot.end.x, y), COLOR_GRID, 1.0)
	var zero_y := _map_y(0.0, plot, top_value, bottom_value)
	_target.draw_line(Vector2(plot.position.x, zero_y), Vector2(plot.end.x, zero_y),
			COLOR_ZERO, 1.0)

	# Left edge, because the right edge is where every trace's live end, marker
	# and label are, and that is the part of the graph being read.
	_text(Vector2(plot.position.x + 3.0, plot.position.y + 11.0),
			_axis_label(top_value), FONT_SIZE_SMALL, COLOR_DIM)
	_text(Vector2(plot.position.x + 3.0, plot.end.y - 2.0),
			_axis_label(bottom_value), FONT_SIZE_SMALL, COLOR_DIM)

	var channels: Array = graph["channels"]
	var series: PackedStringArray = graph["series"]
	var by_wheel := bool(graph["wheels"])
	var width: float = graph["width"]
	for order in channels.size():
		var color := WHEEL_COLORS[order] if by_wheel else _series_color(order)
		var marker := order if by_wheel else (order + 1) % 4
		_draw_series(plot, channels[order], top_value, bottom_value, color, marker,
				series[order] if order < series.size() else "", width)


## One trace, decimated to the plot's pixel width, drawn into the shared scratch
## array so that nothing is allocated here.
func _draw_series(
	plot: Rect2, channel: int, top_value: float, bottom_value: float,
	color: Color, marker: int, label: String, width: float
) -> void:
	if _filled < 2 or _plot_points < 2:
		return
	var buffer: PackedFloat32Array = _history[channel]
	var count := mini(_filled, HISTORY)
	var points := mini(_plot_points, count)
	# Resized rather than sliced so that a redraw allocates nothing. It settles at
	# `_plot_points` once the window has filled, after which this is a no-op.
	if _points.size() != points:
		_points.resize(points)

	# Everything that does not change per point is hoisted, and the y mapping is
	# written out rather than called. This loop runs `points` times for each of 32
	# series on every redraw and is the only part of the panel whose cost is worth
	# measuring — see the profile note at the top of the file.
	var step := float(count - 1) / float(points - 1)
	var pixels_per_point := plot.size.x / float(_plot_points - 1)
	var scale := plot.size.y / (top_value - bottom_value)
	var base := plot.end.y + bottom_value * scale
	var top := plot.position.y
	var bottom := plot.end.y
	# Oldest at the left, newest at the right, and the live end pinned to the
	# right edge: a partly filled window grows leftward instead of stretching four
	# samples across the whole graph.
	var x := plot.end.x - pixels_per_point * float(points - 1)
	var cursor := float(_head + HISTORY * 2) - float(points - 1) * step

	for index in points:
		var y := base - buffer[int(cursor) % HISTORY] * scale
		_points[index] = Vector2(x, clampf(y, top, bottom))
		x += pixels_per_point
		cursor += step
	_target.draw_polyline(_points, color, width)

	var head := _points[points - 1]
	_mark(head, marker, color, 4.0)
	if label != "":
		_text(head + Vector2(-8.0 - _text_width(label, FONT_SIZE_SMALL), 4.0),
				label, FONT_SIZE_SMALL, color)


func _map_y(value: float, plot: Rect2, top_value: float, bottom_value: float) -> float:
	var range_size := top_value - bottom_value
	if range_size <= 0.0:
		return plot.end.y
	var fraction := clampf((value - bottom_value) / range_size, 0.0, 1.0)
	return plot.end.y - plot.size.y * fraction


func _axis_label(value: float) -> String:
	if absf(value) >= 1000.0:
		return "%.0f" % value
	if absf(value) >= 10.0:
		return "%.1f" % value
	return "%.2f" % value


func _series_color(order: int) -> Color:
	return COLOR_CHASSIS if order == 0 else COLOR_SECOND


## The non-color half of §18. Four shapes, one per wheel, drawn at each trace's
## live end and repeated in the numeric table's column header.
func _mark(at: Vector2, shape: int, color: Color, size: float) -> void:
	match shape:
		MARK_SQUARE:
			_target.draw_rect(Rect2(at - Vector2(size, size) * 0.5, Vector2(size, size)), color)
		MARK_TRIANGLE:
			_target.draw_colored_polygon(PackedVector2Array([
				at + Vector2(0.0, -size * 0.7),
				at + Vector2(size * 0.7, size * 0.6),
				at + Vector2(-size * 0.7, size * 0.6),
			]), color)
		MARK_CIRCLE:
			_target.draw_circle(at, size * 0.55, color)
		_:
			_target.draw_colored_polygon(PackedVector2Array([
				at + Vector2(0.0, -size * 0.8),
				at + Vector2(size * 0.7, 0.0),
				at + Vector2(0.0, size * 0.8),
				at + Vector2(-size * 0.7, 0.0),
			]), color)


## Text width, cached.
##
## `Font.get_string_size` shapes the string to measure it, and shaping is the
## single most expensive thing this panel was doing: 44 calls a redraw for
## unchanging labels — graph titles, units, wheel codes — cost 2.0 ms of the
## 3.4 ms a redraw took. They are all constants, so they are measured once.
func _text_width(value: String, size: int) -> float:
	var key := "%d|%s" % [size, value]
	var cached: Variant = _width_cache.get(key)
	if cached != null:
		return cached
	var width := _font.get_string_size(value, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x
	_width_cache[key] = width
	return width


func _text(at: Vector2, value: String, size: int, color: Color) -> void:
	if _font == null:
		return
	_target.draw_string(_font, at, value, HORIZONTAL_ALIGNMENT_LEFT, -1, size, color)


# --- the synthetic source ----------------------------------------------------


## A plausible `VehicleTelemetry` from a tick counter, for exercising the panel
## with no vehicle attached.
##
## Not a placeholder. A display is a piece of software with its own failure modes
## — a trace drawn backwards, a scale that never fits, a banner that never fires —
## and every one of them is easier to find against a signal whose answer is known
## than against a kart. The interesting events are on a schedule here and are rare
## in reality: the inside rear lifts every lap, a shift lands every second and a
## half, the rev limiter is hit, and the frame rate collapses to ADR-0033's
## measured 0.6476 for a second and a half in every twelve.
##
## It is also the contract. If the C++ boundary publishes something this function
## does not produce, one of the two is wrong.
func _demo_sample(tick: int) -> Dictionary:
	var t := float(tick) / 120.0

	# A 7-second lap of one corner: brake, turn, apex, exit.
	var phase := fmod(t, 7.0) / 7.0
	var lateral := sin(phase * TAU) * 1.9
	var longitudinal := -1.4 * exp(-pow((phase - 0.05) * 12.0, 2.0)) \
			+ 0.9 * exp(-pow((phase - 0.62) * 6.0, 2.0))
	var speed := 18.0 + 9.0 * cos(phase * TAU)

	# Static load is a quarter of 175 kg, redistributed by the two accelerations.
	# ARCHITECTURE.md §6: weight transfer is emergent in the real model — here it
	# is faked, because the point is to move the numbers, not to be right.
	var static_load := 175.0 * 9.80665 / 4.0
	var lateral_shift := lateral * 150.0
	var longitudinal_shift := longitudinal * 120.0

	var wheels: Array[Dictionary] = []
	for index in WHEEL_COUNT:
		var left := index == 0 or index == 2
		var front := index < 2
		var load := static_load \
				+ (lateral_shift if left else -lateral_shift) \
				+ (-longitudinal_shift if front else longitudinal_shift)

		# The inside rear leaves the ground at high lateral load, which is the
		# behavior the whole vehicle model exists to produce.
		var lift := 0.0
		var inside_rear := (not front) and (left if lateral < 0.0 else not left)
		if inside_rear and absf(lateral) > 1.45:
			lift = (absf(lateral) - 1.45) * 0.075
		if lift > 0.0:
			load = 0.0
		load = maxf(load, 0.0)

		var slip_angle := deg_to_rad(lateral * (2.6 if front else 1.9)
				+ sin(t * 31.0 + float(index)) * 0.25)
		var slip_ratio := (longitudinal * 0.06 if front else longitudinal * 0.05
				+ (0.05 if longitudinal > 0.0 else 0.0)) \
				+ sin(t * 27.0 + float(index) * 2.0) * 0.004
		var utilization := clampf(
				sqrt(pow(lateral / 2.1, 2.0) + pow(longitudinal / 2.0, 2.0)) * 1.05, 0.0, 1.4)
		wheels.append({
			"normal_load": load,
			"slip_angle": slip_angle,
			"slip_ratio": slip_ratio,
			"suspension_travel": load / 200000.0,
			"lift": lift,
			"utilization": 0.0 if load <= 0.0 else utilization,
			"steer_angle": deg_to_rad(lateral * 4.0) if front else 0.0,
			"force": Vector3(load * lateral * 0.4, -load, load * longitudinal * 0.35),
			"grounded": load > 0.0,
		})

	# A 6-speed sequential with a shift every 1.5 s, wrapping back to first.
	var gear := 1 + (int(t / 1.5) % 6)
	var since_shift := fmod(t, 1.5)
	var rpm := 8600.0 + since_shift * 3800.0
	var shifting := since_shift < 0.06
	var over_rev := rpm > 14000.0

	# ADR-0033 finding 7's measured collapse, so that the alert path is exercised
	# rather than assumed to work.
	var time_ratio := 1.0
	if fmod(t, 12.0) < 1.5:
		time_ratio = 0.6476

	return {
		"wheel": wheels,
		"engine_rpm": rpm,
		"engine_torque": 26.0 + 6.0 * sin(rpm / 900.0) - (18.0 if shifting else 0.0),
		"axle_torque": (26.0 + 6.0 * sin(rpm / 900.0)) * 4.2 * (0.0 if shifting else 1.0),
		"axle_speed": speed / 0.1475,
		"gear": gear,
		"clutch_slip": maxf(0.0, 60.0 - t * 6.0) + (24.0 if shifting else 0.0),
		"clutch_torque": 45.0 if not shifting else 12.0,
		"shifting": shifting,
		"over_rev": over_rev,
		"speed_ms": speed,
		"lateral_g": lateral,
		"longitudinal_g": longitudinal,
		"frame_warp": lateral * 0.0016,
		"substeps": 2,
		"time_ratio": time_ratio,
	}
