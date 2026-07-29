class_name TrackCorridor
extends RefCounted

## How much ground the circuit itself has claimed, station by station. Issue #182.
##
## Scatter and terrain both need one number that neither can get from `KartTrack`
## directly: at a given station, how far from the centerline does the *built*
## circuit reach? Everything inside that is asphalt, curb, verge, run-off apron,
## gravel or barrier, and nothing this file places may go there — a tree in the
## gravel trap is a tree the kart drives through at 120 km/h.
##
## The arithmetic is `KartTrack::surface_meshes`', restated, and that is a
## duplication worth naming rather than hiding. Three numbers come out of the
## collision builder:
##
##   * the road is `width_m` wide, so it reaches `width_m / 2` each side;
##   * the verge is `VERGE_WIDTH` outboard of that, both sides, the whole lap —
##     `circuit::VERGE_MIN_WIDTH_M` in `src/core/track.h`, 1.80 m;
##   * where a corner declares run-off, an apron and then an outfield extend
##     outboard of the verge, over the corner **plus `RUNOFF_LEAD` at each end**,
##     because a kart leaves the road under braking more often than at the apex.
##
## `KartTrack.corner()` publishes `runoff_apron_m` and `runoff_outfield_m` but
## **not `runoff.side`**, so a corner's run-off is reserved on *both* sides here
## while the collider builds it on one. That is deliberately the conservative
## direction — it costs scatter on the inside of six corners and it cannot put a
## prop inside a gravel bed — but it is a duplication defect waiting to happen and
## the fix belongs in `kart_track.cpp`, which this work does not own. Reported.
##
## The table is sampled at `STEP` and read as the larger of the two bracketing
## samples rather than interpolated: it is a *keep-out* distance, so the useful
## error is the one that reserves too much, and taking the maximum does that by
## construction.
##
## **Indexed by forward distance, not by layout station**, and read through
## `to_forward` so callers can keep passing layout stations. It is the same road
## either way and this table describes the road, so it must not change when the
## layout does. Measured before it was: on a table indexed by layout station the
## 1 m quantization landed on different physical points in the two layouts, which
## moved four shrubs, one tree, and the terrain's worst step from 2.382 m to
## 2.685 m — all of it noise from the indexing rather than from the circuit.

## `kart::core::circuit::VERGE_MIN_WIDTH_M`, and `kart_track.cpp` names the same
## constant. A number that has to agree across two files and cannot be shared
## because one of them is C++ and the other is not.
const VERGE_WIDTH := 1.80

## `kart_track.cpp`'s run-off lead-in and lead-out, in meters, either side of the
## corner's own span. Same reason, same duplication.
const RUNOFF_LEAD := 30.0

## One sample per meter. The width taper is authored over tens of meters and the
## run-off spans are hundreds, so a meter resolves both; 1,376 floats is nothing.
const STEP := 1.0

var _track: KartTrack
var _reserved := PackedFloat32Array()
var _length := 0.0
var _max := 0.0


## Walks the lap once and fills the table.
func measure(track: KartTrack) -> void:
	_track = track
	_length = maxf(track.length(), 1.0)
	var count := maxi(1, int(ceil(_length / STEP)))

	# Two arrays because run-off spans overlap: T6 and T7 are 38 m apart and both
	# carry 30 m of lead, so a single array accumulated in place would add the
	# second corner's apron on top of the first corner's total rather than on top
	# of the road.
	var base := PackedFloat32Array()
	base.resize(count)
	_reserved.resize(count)
	for index in count:
		var width := float(track.sample(track.to_station(float(index) * STEP))["width"])
		base[index] = width * 0.5 + VERGE_WIDTH
		_reserved[index] = base[index]

	for corner_index in track.corner_count():
		var corner := track.corner(corner_index)
		if not bool(corner.get("has_runoff", false)):
			continue
		var extra: float = float(corner["runoff_apron_m"]) + float(corner["runoff_outfield_m"])
		if extra <= 0.0:
			continue
		# The corner's two ends as forward distances, then the shorter arc between
		# them. `corner()` reports them in the layout's order, so reversed they
		# arrive swapped; taking the shorter arc makes the span the same physical
		# piece of road either way. A corner is never half a lap long.
		var a := track.to_forward(fposmod(float(corner["from"]), _length))
		var b := track.to_forward(fposmod(float(corner["to"]), _length))
		var span := fposmod(b - a, _length)
		if span > _length * 0.5:
			a = b
			span = _length - span
		var from := a - RUNOFF_LEAD
		span += RUNOFF_LEAD * 2.0
		var walked := 0.0
		while walked <= span:
			var index := _index_of(from + walked)
			_reserved[index] = maxf(_reserved[index], base[index] + extra)
			walked += STEP

	_max = 0.0
	for value in _reserved:
		_max = maxf(_max, value)


## The keep-out radius at a **layout** station, in meters from the centerline.
func reserved_at(station: float) -> float:
	if _reserved.is_empty():
		return 0.0
	return reserved_at_forward(_track.to_forward(fposmod(station, _length)))


## The same, for a caller that already holds a forward distance. Saves the
## conversion in the inner loops, which run tens of thousands of times.
func reserved_at_forward(forward: float) -> float:
	if _reserved.is_empty():
		return 0.0
	var low := _index_of(forward)
	var high := (low + 1) % _reserved.size()
	return maxf(_reserved[low], _reserved[high])


## The widest the circuit gets anywhere. The terrain sizes its flat shoulder from
## this and the scatter sizes its search band from it.
func widest() -> float:
	return _max


func _index_of(station: float) -> int:
	return int(fposmod(station, _length) / STEP) % maxi(1, _reserved.size())
