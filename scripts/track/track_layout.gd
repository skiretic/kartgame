class_name TrackLayout
extends RefCounted

## The test track's centerline: eight segments, four of which are corners, each
## sized from a row of a measurement rather than from taste.
##
## This class knows nothing about meshes, colliders or Godot nodes. It produces a
## list of centerline samples and the distances at which each segment starts and
## ends; `scripts/track/track_ribbon.gd` turns those into geometry and
## `scripts/game/test_track.gd` assembles the scene. The split is so that the
## layout can be reasoned about — and eventually asserted on — without a rendering
## server, which is the same reason `src/core/` has no godot-cpp in it.
##
## ## Why a track shaped like this
##
## `scenes/game/proving_ground.tscn` is deliberately featureless so that a
## surprising kart has no surface to blame, and every §6.4 validation figure is
## measured on it. What it cannot do is let anyone judge the kart by *feel*, which
## is what four M3b tickets are blocked on — #32 "the inside rear visibly lifts",
## #38 "a launch requires clutch modulation", #39 "lifting off in second
## decelerates hard", #40 "turning both assists off makes it demonstrably harder".
##
## So this is not a racing circuit. It is a diagnostic instrument shaped like a
## track, and every corner on it exists to provoke one named behavior. The radii
## come out of `tools/verify/drive_probe.gd`'s recorded steering-lock sweep, which
## is the only place this project has measured what radius the kart can actually
## hold and at what lateral g:
##
##     lock   throttle   sustained g   settled radius   settles at
##     0.25     0.30        0.88 g        18.99 m       46.0 km/h
##     0.25     0.60        1.59 g        32.64 m       81.3 km/h
##     0.25     1.00        1.84 g        58.49 m      116.9 km/h
##     0.40     1.00        1.82 g        50.95 m      108.5 km/h
##     0.60     1.00        1.02 g         3.49 m       21.2 km/h
##     1.00     0.30        0.09 g         2.70 m        5.6 km/h
##
## Two things fall straight out of that table and they are the whole design.
##
## **The radius at which this kart holds its best sustained lateral g is about
## 55 m, not 20 m.** The two full-throttle rows settle at 58.49 m and 50.95 m
## holding 1.84 g and 1.82 g, and 55 m sits between them. A 20 m corner is *not*
## where the kart sits at 1.86 g; the sweep says a 19 m circle is held at 0.88 g,
## because holding a small radius costs steering lock and lock is what the kart
## cannot afford. Turn 4 is therefore 55 m.
##
## **Past a quarter lock the kart falls off a cliff** — 1.02 g at 0.60 lock,
## settling at 21.2 km/h under full throttle. That is
## [#137](https://github.com/skiretic/kartgame/issues/137), it is unresolved, and
## Turn 2 is built to walk straight into it rather than around it.
##
## ## Direction of travel and why the two long corners turn left
##
## Godot's -Z is forward, so heading zero points down -Z and the kart spawns
## facing it. A **positive** turn here is to the right.
##
## Both 180 degree corners turn **left**, which is not arbitrary. `src/core/chassis.h`
## puts the center of mass 41 mm right of the centerline — 27 kg of engine,
## exhaust and radiator hang off that side — so the kart tips at 2.43 g turning
## left against 2.81 g turning right. The lower threshold is the one where the
## inside rear has the best chance of leaving the ground, and #32's acceptance is
## that it visibly does. The two kinks are one right and one left so that the lap
## still contains both.
##
## The loop is closed and symmetric in surface, so it is driveable in both
## directions; driven backwards it becomes two right-handers, which is the cheap
## way to check the 2.81 g side by feel.

## Sampling: how far apart centerline samples are placed on a straight, meters.
##
## Fine enough that a curb ramp (4 m) spans at least two samples, and cheap enough
## not to matter — the whole 1,030 m lap comes out under a thousand samples, which
## is about two thousand road triangles.
const STRAIGHT_SAMPLE_SPACING := 2.0

## Sampling on an arc: the largest gap allowed between the true arc and the chord
## that replaces it, meters.
##
## A chord subtending `t` radians of a radius `R` arc leaves a sagitta of
## `R(1 - cos(t/2))`, which is `R t^2 / 8` for small `t`; inverting it gives the
## angular step below. Stated as a tolerance rather than as a segment count
## because the four corners differ by a factor of five in radius, and a fixed
## count would make the hairpin polygonal and the kink wasteful. 20 mm is a
## quarter of the 80 mm the tire's own contact patch spans.
const ARC_SAGITTA := 0.020

# --- the layout ------------------------------------------------------------
#
# Two of the eight segment lengths are not written down here. `BRAKING_STRAIGHT`
# and `APPROACH_STRAIGHT` are whatever closes the loop, and `_solve_closure()`
# computes them; see its comment. Everything else is a design number with a
# reason attached.

## The start/finish straight, meters. The grid is at its beginning.
##
## Turn 4 exits at around 110 km/h — that is what a 55 m corner held at 1.8 g
## means — so this straight does not have to start from nothing. 220 m from
## 110 km/h puts the kart at the top of its range before the Turn 1 kink: top
## speed is 143.9 km/h and 0-100 takes 4.50 s (ROADMAP M3b), which is 6.2 m/s^2
## averaged over the first 100 km/h and much less above it.
const START_STRAIGHT := 220.0

## Turn 1, the fast kink: radius in meters and turn in degrees, positive right.
##
## Taken flat. At 140 km/h (38.9 m/s) a 120 m radius asks for v^2/r = 12.6 m/s^2,
## which is 1.28 g — inside the 1.84 g the kart demonstrably holds, and asking for
## well under a quarter of steering lock, so it is on the safe side of #137's
## cliff. That is the point: this is the one corner in the lap where turn-in is a
## *transient* question and not a grip question. It is also the fastest thing on
## the track to put a curb against, which is what #139 wants — "a wheel driven at
## it at 100+ km/h does not pass through it".
const KINK_RADIUS := 120.0
const KINK_ANGLE := 40.0

## Turn 2, the hairpin. Left, 180 degrees.
##
## **This corner is built to fail**, because #137 is the milestone's open problem
## and it has to be felt rather than read. The sweep says a quarter of lock
## settles at a 18.99 m radius; with the 8 m road width of `track_ribbon.gd` the
## widest line through an 11 m centerline radius is about 15 m, so the corner
## cannot be taken without going past quarter lock, and past quarter lock the kart
## scrubs off nearly all its speed. 11 m rather than 5 m so that the driver
## arrives at the cliff by degrees instead of stopping dead at the entry — the
## failure has to be legible, not just total.
const HAIRPIN_RADIUS := 11.0
const HAIRPIN_ANGLE := -180.0

## The straight out of the hairpin, meters. The gearbox test.
##
## The hairpin dumps the kart out at 20-30 km/h in first or second, which is the
## only place on the track that exercises a launch-like run up through the gears —
## #38's clutch modulation and #40's auto-shift both live here. 260 m is enough to
## reach the top of the powerband again before Turn 3 and to make a bad exit cost
## something all the way down it.
const POWER_STRAIGHT := 260.0

## Turn 3, the sweeper. Left, 40 degrees.
##
## Deliberately sat **on** #137's boundary rather than either side of it: the
## sweep's 0.25 lock / 0.60 throttle row settles at a 32.64 m radius holding
## 1.59 g at 81.3 km/h, so a 30 m corner is about exactly what a quarter of lock
## buys. A driver who takes it at 80 km/h gets a clean 1.6 g; a driver who arrives
## 10 km/h hot has to add lock and finds the cliff. That is the most useful corner
## on the track for judging whether #137 is a defect or a kart.
const SWEEPER_RADIUS := 30.0
const SWEEPER_ANGLE := -40.0

## Turn 4, the long constant-radius corner. Left, 180 degrees.
##
## The #32 corner. 55 m is where the kart demonstrably holds its best sustained
## lateral g — the sweep's two full-throttle rows settle at 58.49 m / 1.84 g and
## 50.95 m / 1.82 g and this sits between them — and 180 degrees of it is 172.8 m,
## which at 113 km/h is **5.5 seconds** of steady state. That matters: the HUD's
## sustained figure needs 0.5 s of continuous g to credit anything, and #32's
## "the inside rear visibly lifts" is a judgement that cannot be made in a
## flick. Left-handed for the 2.43 g rollover threshold; see the header.
const LONG_RADIUS := 55.0
const LONG_ANGLE := -180.0

## Segment indices, for `segment_start()` and `segment_end()`. Curbs, braking
## boards and the ground query probe are all placed against these rather than
## against a hardcoded distance, so moving a corner moves everything attached to
## it.
enum {
	SEG_START_STRAIGHT,
	SEG_KINK,
	SEG_BRAKING_STRAIGHT,
	SEG_HAIRPIN,
	SEG_POWER_STRAIGHT,
	SEG_SWEEPER,
	SEG_APPROACH_STRAIGHT,
	SEG_LONG_CORNER,
	SEG_COUNT,
}

## The centerline. Each entry is `{position: Vector3, heading: float,
## distance: float}` with `position.y` zero — this class is flat, and elevation is
## M5's business.
var samples: Array[Dictionary] = []

## Cumulative centerline distance at the end of each segment, `SEG_COUNT` long.
var segment_ends := PackedFloat64Array()

## The two lengths `_solve_closure()` found. Reported rather than hidden so that
## the scene can print them and a reviewer can check them by hand.
var braking_straight := 0.0
var approach_straight := 0.0

## How far the walk misses its own start by, meters. Should be at the level of
## double-precision noise; `test_track.gd` refuses to build above a millimeter.
var closure_error := 0.0

## The walk's cursor, carried as **scalars rather than as a `Vector3`**.
##
## `Vector3` stores single-precision components, and a kilometer of walk
## accumulated through 32-bit adds misses its own start by about a millimeter —
## measured at 1.114 mm on the first run of this file, which is a thousand times
## the closure solve's own error and would have been read as a layout that does
## not close. GDScript's `float` is a double, so the walk is carried in two of
## them and only the samples it publishes become `Vector3`.
var _cursor_x := 0.0
var _cursor_z := 0.0
var _heading := 0.0
var _distance := 0.0


func _init() -> void:
	_solve_closure()
	# Measured before the sampled walk, not after: `_terminus()` re-walks and
	# clears `samples` on the way, so asking it afterwards would leave the track
	# with no centerline at all.
	_terminus(braking_straight, approach_straight)
	closure_error = sqrt(_cursor_x * _cursor_x + _cursor_z * _cursor_z)
	_walk(braking_straight, approach_straight, true)
	# The last sample is the first one again. Making them *bit* identical rather
	# than merely close is what keeps the ribbon's seam watertight: the mesh
	# builder places vertices from `position` and `heading`, and a seam that
	# disagrees in the last bit is a hairline crack in the road for a suspension
	# raycast to find. The final heading is -360 degrees by construction, whose
	# cosine is not quite one.
	samples[samples.size() - 1] = samples[0].duplicate()
	samples[samples.size() - 1]["distance"] = _distance


# --- queries ---------------------------------------------------------------


## Total centerline length, meters.
func length() -> float:
	return segment_ends[SEG_COUNT - 1]


func segment_start(segment: int) -> float:
	return 0.0 if segment == 0 else segment_ends[segment - 1]


func segment_end(segment: int) -> float:
	return segment_ends[segment]


## The samples between two centerline distances, inclusive.
##
## Snapped to whichever samples fall in the window rather than interpolated: a
## curb placed by distance lands within one sample spacing of where it was asked
## for, which is 2 m on a straight and under a meter in a corner, and nothing
## about a curb is precise to better than that.
func samples_between(from_distance: float, to_distance: float) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for sample in samples:
		var distance: float = sample["distance"]
		if distance >= from_distance and distance <= to_distance:
			found.append(sample)
	return found


## The sample closest to a centerline distance.
##
## Nearest rather than interpolated, and a linear scan rather than a binary
## search: this is called a couple of dozen times at scene build for the grid, the
## start line and the braking boards, and a scan over 900 samples is free there.
## Anything on the physics path would want the search.
func nearest_sample(distance: float) -> Dictionary:
	var best: Dictionary = samples[0]
	var best_gap := INF
	for sample in samples:
		var gap: float = absf(float(sample["distance"]) - distance)
		if gap < best_gap:
			best_gap = gap
			best = sample
	return best


## The horizontal extent of the centerline, ignoring track width.
func bounds() -> AABB:
	var box := AABB(samples[0]["position"], Vector3.ZERO)
	for sample in samples:
		box = box.expand(sample["position"])
	return box


## The closest approach between two parts of the lap that are far apart along it.
##
## ARCHITECTURE.md §11 lists "closed loop, no self-intersection" as an M5
## validation pass. This is that check, pulled forward and made cheap: pairs
## within `along_threshold` meters of each other *along* the centerline are
## skipped, because a corner is always close to itself. Anything left is two
## different parts of the track, and if the number it returns is smaller than the
## road plus its curbs and verges, they overlap.
func min_separation(along_threshold := 40.0) -> float:
	var total := length()
	var closest := INF
	for i in samples.size():
		var here: Vector3 = samples[i]["position"]
		var here_distance: float = samples[i]["distance"]
		for j in range(i + 1, samples.size()):
			var there_distance: float = samples[j]["distance"]
			var along: float = absf(there_distance - here_distance)
			along = minf(along, total - along)
			if along < along_threshold:
				continue
			closest = minf(closest, here.distance_to(samples[j]["position"]))
	return closest


## Forward at a heading. **This is the coordinate convention and getting it wrong
## is invisible until the kart drives backwards** — heading zero is -Z, which is
## Godot's forward, and `src/core/chassis.h` puts the chassis origin on the ground
## with +Z rearward, so the two agree.
static func forward(heading: float) -> Vector3:
	return Vector3(sin(heading), 0.0, -cos(heading))


## Right at a heading, which is `forward.cross(Vector3.UP)`. Positive lateral
## offsets in `track_ribbon.gd` are this way.
static func right(heading: float) -> Vector3:
	return Vector3(cos(heading), 0.0, sin(heading))


# --- construction ----------------------------------------------------------


## Find the two straight lengths that close the loop.
##
## The eight turns sum to exactly -360 degrees, so the walk always ends pointing
## the way it started; what it does not do for an arbitrary set of lengths is end
## *where* it started. Two straights are left free to fix that, and the
## dependence of the end point on their lengths is exactly linear — a straight
## just translates everything after it — so two probe walks give the derivatives
## and one 2x2 solve gives the answer with no iteration and no tolerance.
##
## Which two: they have to lie along independent directions. `BRAKING_STRAIGHT`
## runs at +40 degrees and `APPROACH_STRAIGHT` at 180, and those are the only two
## independent headings the layout has — the start straight is antiparallel to the
## approach and the power straight is antiparallel to the braking straight, which
## is what happens when a layout is two 180 degree corners and two equal-and-
## opposite kinks. Picking any other pair produces a singular matrix, and this is
## recorded because it was hit twice while the layout was being drawn.
func _solve_closure() -> void:
	_terminus(0.0, 0.0)
	var base_x := _cursor_x
	var base_z := _cursor_z
	_terminus(1.0, 0.0)
	var braking_x := _cursor_x - base_x
	var braking_z := _cursor_z - base_z
	_terminus(0.0, 1.0)
	var approach_x := _cursor_x - base_x
	var approach_z := _cursor_z - base_z

	var determinant := braking_x * approach_z - approach_x * braking_z
	if is_zero_approx(determinant):
		push_error("track layout: the two free straights are parallel, so the loop cannot be closed")
		return
	braking_straight = (-base_x * approach_z + approach_x * base_z) / determinant
	approach_straight = (-braking_x * base_z + base_x * braking_z) / determinant


## Walk to the end without sampling, leaving the answer in `_cursor_x/_cursor_z`.
func _terminus(braking: float, approach: float) -> void:
	_walk(braking, approach, false)


func _walk(braking: float, approach: float, sampled: bool) -> void:
	_cursor_x = 0.0
	_cursor_z = 0.0
	_heading = 0.0
	_distance = 0.0
	samples.clear()
	segment_ends.clear()
	if sampled:
		_push()

	_straight(START_STRAIGHT, sampled)
	_arc(KINK_RADIUS, KINK_ANGLE, sampled)
	_straight(braking, sampled)
	_arc(HAIRPIN_RADIUS, HAIRPIN_ANGLE, sampled)
	_straight(POWER_STRAIGHT, sampled)
	_arc(SWEEPER_RADIUS, SWEEPER_ANGLE, sampled)
	_straight(approach, sampled)
	_arc(LONG_RADIUS, LONG_ANGLE, sampled)


func _straight(segment_length: float, sampled: bool) -> void:
	var start_x := _cursor_x
	var start_z := _cursor_z
	var start_distance := _distance
	var forward_x := sin(_heading)
	var forward_z := -cos(_heading)
	if sampled:
		var steps := maxi(1, int(ceil(segment_length / STRAIGHT_SAMPLE_SPACING)))
		for step in range(1, steps + 1):
			var travelled := segment_length * float(step) / float(steps)
			_cursor_x = start_x + forward_x * travelled
			_cursor_z = start_z + forward_z * travelled
			_distance = start_distance + travelled
			_push()
	else:
		_cursor_x = start_x + forward_x * segment_length
		_cursor_z = start_z + forward_z * segment_length
		_distance = start_distance + segment_length
	segment_ends.append(_distance)


func _arc(radius: float, turn_degrees: float, sampled: bool) -> void:
	var turn := deg_to_rad(turn_degrees)
	var hand := signf(turn)
	# The center is `radius` to the right for a right-hand turn and to the left
	# for a left-hand one, and the traced point is always `radius` back toward the
	# outside of the current heading. Writing it this way rather than as a rotation
	# about the start point means the arc's end lands on the analytic answer rather
	# than on the last of N accumulated rotations.
	var center_x := _cursor_x + cos(_heading) * hand * radius
	var center_z := _cursor_z + sin(_heading) * hand * radius
	var start_heading := _heading
	var start_distance := _distance
	if sampled:
		var step_angle := sqrt(8.0 * ARC_SAGITTA / radius)
		var steps := maxi(2, int(ceil(absf(turn) / step_angle)))
		for step in range(1, steps + 1):
			var swept := turn * float(step) / float(steps)
			_heading = start_heading + swept
			_cursor_x = center_x - cos(_heading) * hand * radius
			_cursor_z = center_z - sin(_heading) * hand * radius
			_distance = start_distance + absf(swept) * radius
			_push()
	else:
		_heading = start_heading + turn
		_cursor_x = center_x - cos(_heading) * hand * radius
		_cursor_z = center_z - sin(_heading) * hand * radius
		_distance = start_distance + absf(turn) * radius
	segment_ends.append(_distance)


func _push() -> void:
	samples.append({
		"position": Vector3(_cursor_x, 0.0, _cursor_z),
		"heading": _heading,
		"distance": _distance,
	})
