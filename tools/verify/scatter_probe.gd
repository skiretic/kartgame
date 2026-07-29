extends SceneTree

## Is the scatter deterministic? Measured, not asserted. Issue #182.
##
##     godot --headless --path . --script res://tools/verify/scatter_probe.gd \
##           -- --out=/tmp/scatter_a.txt
##     godot --headless --path . --script res://tools/verify/scatter_probe.gd \
##           -- --out=/tmp/scatter_b.txt
##     cmp /tmp/scatter_a.txt /tmp/scatter_b.txt
##
## A digest printed by the scene proves the two runs agree on a hash. This dumps
## **every transform**, so the comparison is over the thing itself rather than
## over a summary of it — a hash that matches on two runs of a generator that is
## subtly wrong is a hash that matches.
##
## It runs headless because none of the placement needs a viewport: `KartTrack` is
## the geometry, `TrackTerrain` is arithmetic over it, and `TrackScatter.generate`
## builds transform lists. Only `TrackScatter.build` makes nodes, and this never
## calls it. That is what makes this cost a couple of seconds rather than a window.
##
## Arguments:
##
##   --track=res://data/tracks/valdirone_nuova.track.json
##   --layout=forward       forward or reverse
##   --terrain-cell=5.0     must match the scene's, or the heights differ
##   --out=path             where to write the dump; also printed to stdout as a
##                          summary either way
##   --precision=4          decimal places in the dump. Four is a tenth of a
##                          millimeter, the same quantization `digest()` uses.

var _args := {}


func _initialize() -> void:
	_args = Cmdline.parse()

	var path := Cmdline.as_string(_args, "track", "res://data/tracks/valdirone_nuova.track.json")
	var track := KartTrack.new()
	if track.load(path) != OK:
		for problem in track.problems():
			printerr("  ! ", problem)
		push_error("scatter_probe: %s refused" % path)
		quit(2)
		return

	var layout := Cmdline.as_string(_args, "layout", "forward")
	if not track.select_layout(layout):
		push_error("scatter_probe: no layout %s; have %s" % [layout, track.layout_names()])
		quit(2)
		return

	var started := Time.get_ticks_usec()
	var corridor := TrackCorridor.new()
	corridor.measure(track)
	var corridor_ms := (Time.get_ticks_usec() - started) / 1000.0

	started = Time.get_ticks_usec()
	var terrain := TrackTerrain.new()
	var bounds := _bounds(track)
	var extent := maxf(bounds.size.x, bounds.size.z) + 240.0
	terrain.build(track, corridor, extent, bounds.get_center(), bounds.position.y - 0.5,
			Cmdline.as_float(_args, "terrain-cell", 5.0),
			Cmdline.as_int(_args, "smoothing", TrackTerrain.SMOOTHING_PASSES))
	var terrain_ms := (Time.get_ticks_usec() - started) / 1000.0

	started = Time.get_ticks_usec()
	var scatter := TrackScatter.new()
	scatter.generate(track, corridor, terrain)
	var scatter_ms := (Time.get_ticks_usec() - started) / 1000.0

	print("scatter_probe: %s, %s layout, content %s" % [
		track.track_name(), track.layout(), track.content_hash(),
	])
	print("  corridor  %.1f-%.1f m reserved, built in %.0f ms" % [
		_narrowest(corridor, track), corridor.widest(), corridor_ms,
	])
	print("  terrain   %d verts, %d tris, worst step %.3f m, built in %.0f ms" % [
		terrain.vertex_count(), terrain.triangle_count(), terrain.roughness(), terrain_ms,
	])

	var placement: Dictionary = scatter.placement()
	var keys := placement.keys()
	keys.sort()
	for key in keys:
		print("  %-14s %5d" % [key, (placement[key] as Array).size()])
	print("  total %d objects, %d triangles, %d dropped, digest %s, placed in %.0f ms" % [
		scatter.total(), scatter.triangle_count(), scatter.dropped(), scatter.digest(),
		scatter_ms,
	])

	# The clearance check, which is the other half of what this probe is for. Every
	# object has to be outboard of the corridor, because inboard of it is asphalt,
	# kerb, verge, apron, gravel or barrier — all of which the kart drives on.
	var worst := _worst_intrusion(track, corridor, placement)
	print("  closest approach to the corridor edge: %+.3f m (negative is inside)" % worst)
	if worst < 0.0:
		push_error("scatter_probe: %.3f m inside the reserved corridor" % -worst)

	var out := Cmdline.as_string(_args, "out", "")
	if out != "":
		_dump(out, placement, keys)
	quit(0 if worst >= 0.0 else 1)


func _process(_delta: float) -> bool:
	return true


func _bounds(track: KartTrack) -> AABB:
	var box := AABB()
	var started := false
	for point in track.centerline(0.2, 8.0):
		if not started:
			box = AABB(point, Vector3.ZERO)
			started = true
		else:
			box = box.expand(point)
	return box


func _narrowest(corridor: TrackCorridor, track: KartTrack) -> float:
	var least := 1e30
	var station := 0.0
	while station < track.length():
		least = minf(least, corridor.reserved_at(station))
		station += TrackCorridor.STEP
	return least


## How far the worst-placed object is outside the corridor. Negative means one got
## inside, which is a placement bug and not a tolerance.
func _worst_intrusion(track: KartTrack, corridor: TrackCorridor,
		placement: Dictionary) -> float:
	var worst := 1e30
	for key in placement:
		for transform in placement[key]:
			var origin: Vector3 = transform.origin
			var placed := track.project(origin)
			worst = minf(worst, float(placed["gap"])
					- corridor.reserved_at(float(placed["distance"])))
	return worst


## The dump. Sorted by class then by placement order, so two runs produce two
## byte-identical files or the diff names the first object that moved.
func _dump(path: String, placement: Dictionary, keys: Array) -> void:
	var precision := Cmdline.as_int(_args, "precision", 4)
	var step := pow(10.0, -float(precision))
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_error("scatter_probe: cannot write %s (%d)" % [path, FileAccess.get_open_error()])
		return
	var format := "%%s %%d %%.%df %%.%df %%.%df %%.%df %%.%df %%.%df %%.%df %%.%df %%.%df %%.%df %%.%df %%.%df" % [
		precision, precision, precision, precision, precision, precision,
		precision, precision, precision, precision, precision, precision,
	]
	for key in keys:
		var transforms: Array = placement[key]
		for index in transforms.size():
			var transform: Transform3D = transforms[index]
			var basis: Basis = transform.basis
			file.store_line(format % [
				key, index,
				_clean(basis.x.x, step), _clean(basis.x.y, step), _clean(basis.x.z, step),
				_clean(basis.y.x, step), _clean(basis.y.y, step), _clean(basis.y.z, step),
				_clean(basis.z.x, step), _clean(basis.z.y, step), _clean(basis.z.z, step),
				_clean(transform.origin.x, step), _clean(transform.origin.y, step),
				_clean(transform.origin.z, step),
			])
	file.close()
	print("  dumped %s" % path)


## Snapped to the print step, then forced off negative zero.
##
## `%.4f` writes `-0.0000` for anything in [-5e-5, -0) and `0.0000` for its
## positive twin, so two placements that agree to 1e-18 produce dumps that `cmp`
## calls different. That cost a false negative on the forward-against-reverse
## comparison: 5,135 objects reported as differing, worst numeric difference
## exactly zero at nine decimal places. Snapping first turns the tiny negative
## into a negative zero and the `+ 0.0` turns that into a positive one, which is
## the only ordering that fixes both cases.
func _clean(value: float, step: float) -> float:
	return snappedf(value, step) + 0.0
