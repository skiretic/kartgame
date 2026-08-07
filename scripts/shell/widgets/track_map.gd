class_name TrackMap
extends Control

## The circuit, in plan, drawn from `KartTrack.centerline()`. Mockup plate 6's
## right-hand column.
##
## **There is no committed path data and no SVG intermediate.** The plate's map is
## a literal `<path d="M 761.3 443.4 L ...">` because a mockup has no engine under
## it; this widget takes the same walk the collider and the mesh take. That is the
## whole reason it exists as a widget rather than as an exported asset: an authored
## corner that moves moves the map, and a map that disagreed with the track would
## be a picture nobody could tell was stale.
##
## `docs/circuits/valdirone_nuova_centerline.csv` is **not** the source and must
## never become it — CLAUDE.md: it drifts up to 0.49 m from the exact walk of the
## design's own segment list. `KartTrack` is normative.
##
## ## Projection
##
## `centerline()` returns world-space `Vector3(x, elevation, z)`, so the plan view
## is the x/z pair and the y component is dropped. Screen x is world +X and screen
## y is world +Z, which puts Godot's forward (-Z, per CLAUDE.md's coordinate note)
## **up the page** — a kart leaving the start line drives toward the top of the
## widget, which is the orientation every circuit map in the sport uses.
##
## Aspect is preserved by construction: one scalar `scale` for both axes, chosen
## as the smaller of the two fits. A per-axis fit would stretch the map to the
## widget's shape and quietly redraw a hairpin as a sweeper.
##
## ## What it is not
##
## `_draw()` cannot hold focus — `shell_screen.gd`'s header says so and it is why
## the rest of the shell is a real `Container`/`Button` tree. This widget is not
## interactive and takes `FOCUS_NONE`, so a screen that puts it in a column must
## keep something reachable elsewhere or the pad has nowhere to go on that side.

## Inset from the widget's own rect, in pixels at the design scale.
##
## Half the stroke width plus a little, so the outermost point of the loop is drawn
## whole rather than clipped down its centerline by the edge of the rect.
const MARGIN_PX := 6.0

## Stroke width, as a fraction of the drawn circuit's own width.
##
## **Derived from the plate**, and as a ratio rather than a pixel count on purpose:
## its path spans `x` 238.7 to 761.3 — 522.6 user units — at `stroke-width: 9`, so
## the line is `9 / 522.6 = 0.0172` of the map's width whatever size the map is
## drawn at. Ported as 3.86 px (the plate's own rendered figure at `max-width:
## 15rem`) the line would come out a third too thin here, because this sheet draws
## the map half again as wide as the mockup does.
const STROKE_FRACTION := 0.0172

## Never thinner than this, pixels. A widget squeezed into a narrow column would
## otherwise scale its own line away to nothing.
const STROKE_MIN_PX := 1.5

## The start-line tick, as multiples of the stroke. The plate draws `x1="748"
## x2="774"` at `stroke-width: 6` against the path's 9 — so the tick is `26 / 9 =
## 2.89` long and `6 / 9 = 0.67` thick, and it is centered on the first point.
##
## It reads as a line laid across the track precisely because it is nearly three
## times the track line's width and thinner than it. Both figures matter; a tick at
## 1.5 thick and 2.9 long is a square blob, which is what this drew first.
const TICK_LENGTH := 2.89
const TICK_WIDTH := 0.67

## A closed lap's polyline already returns to its first point (measured on
## Valdirone: `centerline(0.2, 8.0)[0]` and `[221]` are both exactly the origin),
## but a layout whose walk stops a step short would otherwise draw with a gap in
## it. Closed by hand when the two ends are further apart than this, in meters.
const CLOSE_TOLERANCE_M := 0.001

var _points: PackedVector2Array = PackedVector2Array()
var _ink: Color = ShellTheme.PAPER_INK
var _fitted := Rect2()


func _init() -> void:
	focus_mode = Control.FOCUS_NONE
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	resized.connect(queue_redraw)


## The circuit, as `KartTrack.centerline()` returns it. Elevation is dropped here
## rather than by the caller, so the caller passes the array it was given.
func set_centerline(line: PackedVector3Array) -> void:
	_points = PackedVector2Array()
	for point: Vector3 in line:
		_points.append(Vector2(point.x, point.z))
	if _points.size() >= 2:
		var first := _points[0]
		var last := _points[_points.size() - 1]
		if first.distance_to(last) > CLOSE_TOLERANCE_M:
			_points.append(first)
	queue_redraw()


## The ink to draw in. A token from `ShellTheme`, passed rather than assumed,
## because the same widget belongs on a paper sheet and on a screen-family panel
## and those are two different inks.
func set_ink(color: Color) -> void:
	_ink = color
	queue_redraw()


## How many centerline points are being drawn. For the gate and the report — a map
## that silently fell back to an empty array draws nothing and looks like a layout
## bug rather than a load failure.
func point_count() -> int:
	return _points.size()


## The rect the fitted circuit actually occupies inside this widget, in local
## pixels. Empty until the first `_draw()`.
func fitted_rect() -> Rect2:
	return _fitted


func _draw() -> void:
	if _points.size() < 2:
		return
	var bounds := _bounds()
	if bounds.size.x <= 0.0 and bounds.size.y <= 0.0:
		return

	var inner := Vector2(size.x - 2.0 * MARGIN_PX, size.y - 2.0 * MARGIN_PX)
	if inner.x <= 0.0 or inner.y <= 0.0:
		return
	# One scalar for both axes. A degenerate span on either axis — a circuit drawn
	# as a straight line — would divide by zero, so that axis is simply not the
	# binding one.
	var scale := INF
	if bounds.size.x > 0.0:
		scale = minf(scale, inner.x / bounds.size.x)
	if bounds.size.y > 0.0:
		scale = minf(scale, inner.y / bounds.size.y)
	if not is_finite(scale) or scale <= 0.0:
		return

	var drawn := bounds.size * scale
	var origin := (size - drawn) * 0.5 - bounds.position * scale
	_fitted = Rect2((size - drawn) * 0.5, drawn)

	var screen := PackedVector2Array()
	screen.resize(_points.size())
	for index: int in _points.size():
		screen[index] = _points[index] * scale + origin
	var stroke := maxf(STROKE_MIN_PX, drawn.x * STROKE_FRACTION)
	draw_polyline(screen, _ink, stroke, true)

	# The start line. Perpendicular to the first segment, centered on the first
	# point, in the accent — the plate's own `<line stroke="var(--accent)">`. Taken
	# from the first *segment* and not from a stored heading, because the widget is
	# handed a polyline and nothing else; at 0.2 m sagitta the first segment is at
	# most 8 m long and its direction is the track's to well under a degree.
	var heading := (screen[1] - screen[0]).normalized()
	if heading.length_squared() > 0.0:
		var across := Vector2(-heading.y, heading.x) * stroke * TICK_LENGTH * 0.5
		draw_line(screen[0] - across, screen[0] + across, ShellTheme.ACCENT,
				stroke * TICK_WIDTH, true)


func _bounds() -> Rect2:
	var low := _points[0]
	var high := _points[0]
	for point: Vector2 in _points:
		low.x = minf(low.x, point.x)
		low.y = minf(low.y, point.y)
		high.x = maxf(high.x, point.x)
		high.y = maxf(high.y, point.y)
	return Rect2(low, high - low)
