class_name TrackScatter
extends RefCounted

## Trackside dressing, placed from `track.json` and a seeded PRNG. Issue #182,
## ROADMAP M5's "runtime scatter: seeded Poisson-disk props and foliage".
##
## ## Why none of it is hand-placed
##
## The circuit is 1,375 m long inside a 402 m bounding box and re-authoring it is
## one command — edit the design, `author_track.py`, `gentrack.sh`, `circuit.sh`.
## Anything positioned by hand survives none of that. So every object here is a
## function of `(station, side, lateral offset)` and the arithmetic that turns
## those into a world point is `KartTrack`'s own, which means a corner that moves
## takes its trees with it.
##
## ## Determinism
##
## `src/core/pcg32.h` through `KartRandom`, and every class of object carries its
## **own explicitly seeded stream** — ARCHITECTURE.md §8 rule 3. That is not
## decoration here: with one shared generator, changing the tree count would move
## every shrub, because the shrubs would be drawing from wherever the trees
## happened to stop. With one stream each, the two are independent.
##
## The seed is the track's **content hash**, folded to 64 bits. Two runs of the
## same file place the same objects; a file whose corner moved gets new scatter,
## which is correct — the old placement was a function of geometry that no longer
## exists. Engine `RandomNumberGenerator` is deliberately not used, per
## `kart_random.h`: its state is not ours to pin across engine versions.
##
## `tools/verify/scatter_probe.gd` is the measurement. It dumps every transform
## and the run is compared against itself byte for byte.
##
## ## Where an object may go
##
## Outboard of `TrackCorridor.reserved_at(station)` and never inboard of it. That
## keeps every prop clear of the asphalt, the curbs, the verge, the run-off apron,
## the gravel and the barrier — so nothing is in a suspension raycast's path and
## nothing is on the racing surface. Objects carry **no colliders at all**: a kart
## that clears the barrier drives through a tree. That is a deliberate stop, not
## an oversight, because a new static body beside the track is a new way to move a
## measured figure, and the barrier that is already there is the collider that
## matters. Reported as a ticket rather than left in this comment.
##
## Height comes from `TrackTerrain`, evaluated at the object's own point, so a
## prop sits on the drawn ground rather than at the elevation of the road beside
## it — on a 90 m falloff those differ by meters.
##
## ## Why MultiMesh
##
## 5,187 instances is 5,187 draw calls as `MeshInstance3D` and seven as
## `MultiMeshInstance3D`. The cost is that `LightmapGI` cannot bake a
## `MultiMeshInstance3D` — it walks `MeshInstance3D` only — so scatter takes its
## indirect light from the lightmap's **probes** instead, which is what
## `generate_probes_subdiv` is for. See `docs/adr_pending_182.md`.

## The PCG32 streams. Distinct constants rather than 0, 1, 2 — `pcg32.h` requires
## the increment to be odd and forces it, so neighboring even/odd pairs would
## collapse onto the same stream and the whole point of separating them is lost.
const STREAM_TREE := 0x5472_6565_0000_0001
const STREAM_SHRUB := 0x5368_7275_0000_0003
const STREAM_JITTER := 0x4a69_7474_0000_0005

## Along-lap resolution of the cached frame table, meters. Placement draws a table
## *index*, not a float station, so the sampling is exactly reproducible and the
## inner loop touches no C++ at all. Two meters is far finer than the smallest
## object placed.
const STATION_STEP := 2.0

## Trees. `INNER` is meters outboard of the reserved corridor — enough that a
## canopy overhanging by a meter still does not reach the barrier — and `OUTER`
## is where the band stops, chosen against `TrackTerrain.FALLOFF` so the far edge
## of the wood is still on ground that has not yet relaxed to the base plane.
const TREE_INNER := 6.0
const TREE_OUTER := 72.0
## Poisson-disk minimum separation, meters. Below about 6 m the canopies at the
## radii used here interpenetrate and the wood reads as one solid mass.
const TREE_SPACING := 8.0
const TREE_TRUNK_RADIUS := 0.22
const TREE_HEIGHT_LOW := 6.0
const TREE_HEIGHT_HIGH := 12.5

## Shrubs fill the strip between the barrier and the tree line, which would
## otherwise be mown lawn from the curb to the wood.
const SHRUB_INNER := 1.5
const SHRUB_OUTER := 22.0
const SHRUB_SPACING := 3.5
const SHRUB_RADIUS_LOW := 0.55
const SHRUB_RADIUS_HIGH := 1.45

## Dart-throwing budget as a multiple of the disks that would fit at the given
## spacing. Rejection sampling is Poisson-disk sampling; twenty tries per expected
## acceptance is where the yield curve flattens on this band.
const ATTEMPT_FACTOR := 20

## Marshal posts, one at each end of each corner, on the **outside** of it. The
## side is derived from the corner's `hand` rather than from `runoff.side`,
## because `KartTrack.corner()` does not publish the latter — see `TrackCorridor`.
const POST_HEIGHT := 2.6
const POST_RADIUS := 0.06
const POST_STANDOFF := 1.2

## Braking boards, at these distances before each corner's entry. Not a
## regulation figure and not presented as one: they are the three round numbers a
## driver reads off a board, and they exist to give the approach a rhythm rather
## than to certify anything.
const BOARD_DISTANCES: PackedFloat64Array = [150.0, 100.0, 50.0]
const BOARD_STANDOFF := 1.4
const BOARD_HEIGHT := 1.55

## Tire stacks along the barrier of every corner that declares run-off, placed
## **outboard** of it so the barrier's own collider is always between them and the
## kart. Face-on they read as the tire wall; from behind they are what they are.
const TIRE_PITCH := 2.4
const TIRE_STANDOFF := 0.75
const TIRE_RADIUS := 0.33
const TIRE_HEIGHT := 0.21
const TIRE_ROWS := 4

var _track: KartTrack
var _corridor: TrackCorridor
var _terrain: TrackTerrain

var _positions := PackedVector3Array()
var _rights := PackedVector3Array()
var _reserved := PackedFloat32Array()
var _length := 0.0

## +1 forward, -1 reversed. Every lateral offset in this file is in the **forward**
## frame, so a wood is in the same field whichever way the circuit is driven — see
## `_cache_frames`.
var _hand := 1.0

## Placement, per class, as flat transform lists. Kept rather than discarded into
## the `MultiMesh` so `scatter_probe.gd` can dump them without a viewport.
var _placed := {}

## Deterministic props — posts, boards, tire stacks — that landed inside another
## part of the circuit's corridor and were dropped rather than moved. Reported so
## a number that starts climbing is visible rather than silent.
var _dropped := 0


func generate(track: KartTrack, corridor: TrackCorridor, terrain: TrackTerrain) -> void:
	_track = track
	_corridor = corridor
	_terrain = terrain
	_length = maxf(track.length(), 1.0)
	_cache_frames()

	var seed_value := _seed_from(track.content_hash())
	_placed = {}
	_dropped = 0
	_place_trees(seed_value)
	_place_shrubs(seed_value)
	_place_marshal_posts()
	_place_boards()
	_place_tire_stacks()


## What was placed, by class. `{name: Array[Transform3D]}`.
func placement() -> Dictionary:
	return _placed


func total() -> int:
	var sum := 0
	for key in _placed:
		sum += (_placed[key] as Array).size()
	return sum


func dropped() -> int:
	return _dropped


## Triangles the scatter adds, summed over every instance.
##
## Worth having as a number because frame time is not: measured at 1920x1080 with
## the whole circuit in frame, the difference between scatter on and scatter off
## is smaller than the run-to-run spread of `shoot.sh`'s own median. A count of
## triangles and of draw calls says what was added; a frame time that moves 1.8 ms
## between two identical runs does not.
func triangle_count() -> int:
	var per_instance := {
		"Tree": _faces(_trunk_mesh()) + _faces(_canopy_mesh()),
		"Shrub": _faces(_shrub_mesh()),
		"MarshalPost": _faces(_post_mesh()),
		"MarshalPanel": _faces(_panel_mesh()),
		"Board": _faces(_board_mesh()),
		"Tire": _faces(_tire_mesh()),
	}
	var total_faces := 0
	for key in per_instance:
		total_faces += int(per_instance[key]) * (_placed.get(key, []) as Array).size()
	return total_faces


func _faces(mesh: Mesh) -> int:
	var arrays := mesh.surface_get_arrays(0)
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	if indices.is_empty():
		return (arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array).size() / 3
	return indices.size() / 3


## FNV-1a over every transform, quantized to a tenth of a millimeter.
##
## Quantized because the point of the digest is to compare two *processes*, and
## an exact float compare would be reporting the platform's rounding rather than
## the placement. A tenth of a millimeter is four orders finer than anything that
## could be called the same placement.
func digest() -> String:
	# FNV-1a's 64-bit offset basis is 0xcbf29ce484222325, which is over 2^63 and so
	# is not a writable GDScript integer literal — `hex_to_int` refuses it outright.
	# Written as the signed value with the same bit pattern; the arithmetic below is
	# two's complement either way, and the mask FNV specifies is what a 64-bit
	# signed multiply already does on overflow.
	var hash_value := -3750763034362895579
	for key in _sorted_classes():
		for transform in _placed[key]:
			var basis: Basis = transform.basis
			for vector in [basis.x, basis.y, basis.z, transform.origin]:
				for component in [vector.x, vector.y, vector.z]:
					var quantized := int(round(component * 10000.0))
					for byte_index in 8:
						hash_value ^= (quantized >> (byte_index * 8)) & 0xff
						hash_value = hash_value * 0x100000001b3
	# `String.pad_zeros` counts only digit characters, so a hex string starting
	# with a letter is padded in the wrong place. Padded by hand, exactly as
	# `KartStateHash::hex` does.
	var text := String.num_uint64(hash_value, 16)
	while text.length() < 16:
		text = "0" + text
	return text


func _sorted_classes() -> Array:
	var keys := _placed.keys()
	keys.sort()
	return keys


# --- placement -------------------------------------------------------------


## Positions, right-vectors and reserved widths every `STATION_STEP`, once.
##
## The dart-throwing loop runs tens of thousands of times; doing it against this
## table rather than against `KartTrack.sample()` keeps a `Dictionary` allocation
## and a spline evaluation out of the inner loop, and — more importantly — makes
## the draw an integer index, so two runs sample identical points by construction
## rather than by float luck.
##
## **Indexed by forward distance, not by layout station.** A `KartTrack` selected
## in reverse renumbers the lap and turns every heading by half a revolution, so a
## table built on layout stations puts the wood in a different field depending on
## which way the circuit is being driven — which is wrong: it is the same wood.
## `to_station` converts the index, and `_hand` undoes the heading flip so that a
## lateral offset here always means the same side of the same road. `--layout` is
## then measurable rather than assumed: the two dumps agree object for object
## except for the braking boards, which belong to the direction of travel.
func _cache_frames() -> void:
	_hand = -1.0 if _track.is_reversed() else 1.0
	var count := maxi(2, int(ceil(_length / STATION_STEP)))
	_positions.resize(count)
	_rights.resize(count)
	_reserved.resize(count)
	for index in count:
		var forward := float(index) * STATION_STEP
		var frame := _track.sample(_track.to_station(forward))
		_positions[index] = frame["position"]
		var heading: float = frame["heading"]
		# `Track::right_of`: right is (cos h, 0, sin h), forward is (sin h, 0, -cos h).
		# Restated here rather than derived, because a sign error puts every prop
		# on the far side of the circuit and the render still looks plausible.
		_rights[index] = Vector3(cos(heading), 0.0, sin(heading)) * _hand
		_reserved[index] = _corridor.reserved_at_forward(forward)


## A world point at a forward-station index and a signed lateral offset, standing
## on the terrain rather than on the road.
func _point(index: int, lateral: float) -> Vector3:
	var wrapped := index % _positions.size()
	var flat := _positions[wrapped] + _rights[wrapped] * lateral
	return Vector3(flat.x, _terrain.height_at(flat.x, flat.z), flat.z)


func _reserved_at_index(index: int) -> float:
	return _reserved[index % _reserved.size()]


## The nearest forward-station index to a layout station.
func _slot_of(station: float) -> int:
	var forward := _track.to_forward(fposmod(station, _length))
	return int(round(fposmod(forward, _length) / STATION_STEP)) % _positions.size()


## Is this point outside the corridor of the **nearest** part of the lap?
##
## Not the station it was placed from — the nearest one, which is a different
## question wherever the circuit passes close to itself, and Valdirone does that
## by design. Measured before this existed: 1,757 trees placed 6 m outboard of the
## straight they were drawn against, and the worst of them was **34.97 m inside**
## T8's run-off, which is a tree in the gravel trap at the end of the fastest
## approach on the circuit. Placement against a local width is not placement
## against the circuit.
func _clear(point: Vector3, margin: float) -> bool:
	var placed := _track.project(point)
	return float(placed["gap"]) >= _corridor.reserved_at(float(placed["distance"])) + margin


## Dart-throwing Poisson-disk over the band, with a spatial hash for the test.
##
## The hash cell is the spacing itself, so a candidate only ever has to look at
## the nine cells around it — without that this is O(n^2) and two thousand trees
## is four million distance tests.
func _throw_darts(rng: KartRandom, key: String, inner: float, outer: float,
		spacing: float) -> PackedVector3Array:
	var accepted := PackedVector3Array()
	var cells := {}
	var band := maxf(outer - inner, 0.1)
	var area := _length * band * 2.0
	# Disk-packing density for a Poisson-disk process, ~1 / (0.7 r^2). Only used to
	# size the attempt budget, so being approximate costs a few wasted draws.
	var expected := int(area / (0.7 * spacing * spacing))
	var attempts := expected * ATTEMPT_FACTOR

	for _attempt in attempts:
		var index := int(rng.next_below(_positions.size()))
		var side := 1.0 if rng.next_below(2) == 1 else -1.0
		var offset := _reserved_at_index(index) + inner + rng.next_range(0.0, band)
		var point := _point(index, side * offset)

		var cell_x := int(floor(point.x / spacing))
		var cell_z := int(floor(point.z / spacing))
		var clear := true
		for dx in range(-1, 2):
			for dz in range(-1, 2):
				var bucket: PackedVector3Array = cells.get(Vector2i(cell_x + dx, cell_z + dz),
						PackedVector3Array())
				for other in bucket:
					# Horizontal distance only. Two trees on a bank are neighbors on
					# the ground however far apart their trunks' bases are in Y.
					if Vector2(point.x - other.x, point.z - other.z).length() < spacing:
						clear = false
						break
				if not clear:
					break
			if not clear:
				break
		if not clear:
			continue
		# The corridor test runs *after* the spacing test, and the order is worth
		# 2.1 s of scene load. Both have to pass, so correctness does not care —
		# but the spacing test is arithmetic on a `PackedVector3Array` and the
		# corridor test is a `project` across the C++ boundary with a `Dictionary`
		# coming back, and once the band is full the spacing test rejects most of
		# the 212,000 darts before the expensive one is reached.
		if not _clear(point, inner):
			continue

		var bucket_key := Vector2i(cell_x, cell_z)
		if not cells.has(bucket_key):
			cells[bucket_key] = PackedVector3Array()
		var bucket: PackedVector3Array = cells[bucket_key]
		bucket.append(point)
		cells[bucket_key] = bucket
		accepted.append(point)

	_placed[key] = []
	return accepted


func _place_trees(seed_value: int) -> void:
	var rng := KartRandom.new()
	rng.seed(seed_value, STREAM_TREE)
	var points := _throw_darts(rng, "Tree", TREE_INNER, TREE_OUTER, TREE_SPACING)

	# A second stream for size and spin. Drawing them from the placement stream
	# would work and would couple the two: a change to the size range would then
	# move every tree, which is exactly the coupling §8 rule 3 forbids.
	var shape := KartRandom.new()
	shape.seed(seed_value, STREAM_JITTER)
	var transforms: Array[Transform3D] = []
	for point in points:
		var height := shape.next_range(TREE_HEIGHT_LOW, TREE_HEIGHT_HIGH)
		var spin := shape.next_range(0.0, TAU)
		# Non-uniform: a tree leaning is a tree, a tree scaled unevenly on two axes
		# is a mistake. Height varies, girth follows it at a fixed ratio.
		var basis := Basis(Vector3.UP, spin).scaled(Vector3(
			height / TREE_HEIGHT_LOW, height / TREE_HEIGHT_LOW, height / TREE_HEIGHT_LOW))
		transforms.append(Transform3D(basis, point))
	_placed["Tree"] = transforms


func _place_shrubs(seed_value: int) -> void:
	var rng := KartRandom.new()
	rng.seed(seed_value, STREAM_SHRUB)
	var points := _throw_darts(rng, "Shrub", SHRUB_INNER, SHRUB_OUTER, SHRUB_SPACING)

	var transforms: Array[Transform3D] = []
	for point in points:
		var radius := rng.next_range(SHRUB_RADIUS_LOW, SHRUB_RADIUS_HIGH)
		var spin := rng.next_range(0.0, TAU)
		# Squashed on Y: an unsquashed sphere at this size reads as a boulder.
		var basis := Basis(Vector3.UP, spin).scaled(Vector3(radius, radius * 0.65, radius))
		transforms.append(Transform3D(basis, point))
	_placed["Shrub"] = transforms


## The outside of a corner, as a lateral sign.
##
## A left-hander's outside is its right. Derived from `hand` because
## `KartTrack.corner()` does not publish `runoff.side`; checked against the file
## for all eight corners of Valdirone, where the two agree everywhere except the
## two esse corners whose run-off is on both sides and where either answer is
## right.
##
## Multiplied by `_hand` so the answer is in the forward frame like every other
## lateral offset here. Reversed, `corner()` reports the other hand *and* the
## other right, and the two flips cancel — which is what makes a marshal post
## stand in the same place whichever way the circuit is driven.
func _outside_of(corner: Dictionary) -> float:
	return (1.0 if String(corner["hand"]) == "left" else -1.0) * _hand


func _place_marshal_posts() -> void:
	var posts: Array[Transform3D] = []
	var panels: Array[Transform3D] = []
	for index in _track.corner_count():
		var corner := _track.corner(index)
		var side := _outside_of(corner)
		# Sorted, so the two ends are visited in the same order in both layouts and
		# the dumps line up object for object rather than merely as a set.
		var ends := [_slot_of(float(corner["from"])), _slot_of(float(corner["to"]))]
		ends.sort()
		for slot in ends:
			var offset := _reserved_at_index(slot) + POST_STANDOFF
			var base := _point(slot, side * offset)
			# Same nearest-station test the darts get, and for the same reason: the
			# esse's two corners are 38 m apart, so the outside of T7 is close
			# enough to T6 to land inside its run-off.
			if not _clear(base, 0.0):
				_dropped += 1
				continue
			# Facing the track: the panel's -Z looks back across the run-off, so a
			# marshal post seen from the racing line is seen face on.
			var facing := -_rights[slot] * side
			var basis := Basis().looking_at(facing, Vector3.UP)
			posts.append(Transform3D(basis, base))
			panels.append(Transform3D(basis, base + Vector3(0.0, POST_HEIGHT * 0.82, 0.0)))
	_placed["MarshalPost"] = posts
	_placed["MarshalPanel"] = panels


func _place_boards() -> void:
	var posts: Array[Transform3D] = _placed.get("MarshalPost", [] as Array[Transform3D])
	var boards: Array[Transform3D] = []
	for index in _track.corner_count():
		var corner := _track.corner(index)
		var side := _outside_of(corner)
		for back in BOARD_DISTANCES:
			# The one thing here that is deliberately **not** layout-invariant. A
			# braking board is read on the approach, so it belongs to the direction
			# of travel: reversed, the entry is the other end of the corner and the
			# boards move to the other side of it. `corner["from"]` is already the
			# layout's entry, so subtracting works unchanged in both.
			var slot := _slot_of(float(corner["from"]) - back)
			var offset := _reserved_at_index(slot) + BOARD_STANDOFF
			var base := _point(slot, side * offset)
			if not _clear(base, 0.0):
				_dropped += 1
				continue
			var facing := -_rights[slot] * side
			var basis := Basis().looking_at(facing, Vector3.UP)
			posts.append(Transform3D(basis, base))
			boards.append(Transform3D(basis, base + Vector3(0.0, BOARD_HEIGHT, 0.0)))
	_placed["MarshalPost"] = posts
	_placed["Board"] = boards


## Tires along the barrier line of every corner that declares run-off.
##
## Placed at `reserved + TIRE_STANDOFF`, which is outboard of the barrier the
## collider builds at `reserved` exactly. Nothing here can be reached without
## first going through a wall that does have a collider.
func _place_tire_stacks() -> void:
	var tires: Array[Transform3D] = []
	for index in _track.corner_count():
		var corner := _track.corner(index)
		if not bool(corner.get("has_runoff", false)):
			continue
		var side := _outside_of(corner)
		# Walked in **forward** distance, from the lower of the corner's two forward
		# stations. Walking it in layout stations would start at the other end
		# reversed, and the rounding to `STATION_STEP` would then land on a
		# different set of slots — a tire wall that is one stack longer driven
		# backwards, which is the sort of difference nobody would ever look for.
		var a := _track.to_forward(fposmod(float(corner["from"]), _length))
		var b := _track.to_forward(fposmod(float(corner["to"]), _length))
		var span := fposmod(b - a, _length)
		if span > _length * 0.5:
			a = b
			span = _length - span
		var walked := 0.0
		while walked <= span:
			var slot := int(round(fposmod(a + walked, _length) / STATION_STEP)) % _positions.size()
			var offset := _reserved_at_index(slot) + TIRE_STANDOFF
			var base := _point(slot, side * offset)
			if not _clear(base, 0.0):
				_dropped += 1
				walked += TIRE_PITCH
				continue
			for row in TIRE_ROWS:
				tires.append(Transform3D(
					Basis.IDENTITY,
					base + Vector3(0.0, TIRE_HEIGHT * (0.5 + float(row)), 0.0)
				))
			walked += TIRE_PITCH
	_placed["Tire"] = tires


## The content hash, folded into a seed.
##
## `content_hash()` is FNV-1a over the file's bytes as sixteen hex characters.
## Parsing it back is what ties the placement to the geometry: a circuit whose
## corner moved gets new scatter, because the placement was a function of a file
## that no longer exists.
func _seed_from(hex: String) -> int:
	var value := 0
	for character in hex:
		var digit := "0123456789abcdef".find(character.to_lower())
		if digit < 0:
			continue
		value = ((value << 4) | digit) & 0x7fffffffffffffff
	return value


# --- nodes -----------------------------------------------------------------


## Everything placed, as `MultiMeshInstance3D` nodes under one parent.
func build(parent: Node3D) -> Node3D:
	var root := Node3D.new()
	root.name = "Scatter"
	parent.add_child(root)

	root.add_child(_multimesh("TreeTrunk", _trunk_mesh(), _bark_material(),
			_placed.get("Tree", []), Vector3.ZERO))
	# The canopy rides at 0.62 of the trunk's height. Instance transforms carry the
	# tree's scale, so a local offset in the *mesh* would be scaled with it, which
	# is what is wanted: a taller tree's canopy sits higher.
	root.add_child(_multimesh("TreeCanopy", _canopy_mesh(), _foliage_material(),
			_placed.get("Tree", []), Vector3.ZERO))
	root.add_child(_multimesh("Shrub", _shrub_mesh(), _foliage_material(),
			_placed.get("Shrub", []), Vector3.ZERO))
	root.add_child(_multimesh("MarshalPost", _post_mesh(), _post_material(),
			_placed.get("MarshalPost", []), Vector3.ZERO))
	root.add_child(_multimesh("MarshalPanel", _panel_mesh(), _marshal_material(),
			_placed.get("MarshalPanel", []), Vector3.ZERO))
	root.add_child(_multimesh("Board", _board_mesh(), _board_material(),
			_placed.get("Board", []), Vector3.ZERO))
	root.add_child(_multimesh("Tire", _tire_mesh(), _tire_material(),
			_placed.get("Tire", []), Vector3.ZERO))
	return root


func _multimesh(node_name: String, mesh: Mesh, material: Material,
		transforms: Array, offset: Vector3) -> MultiMeshInstance3D:
	var multi := MultiMesh.new()
	multi.transform_format = MultiMesh.TRANSFORM_3D
	multi.mesh = mesh
	# After `mesh` and `transform_format`, in that order — setting the count first
	# throws the buffer away when the format changes.
	multi.instance_count = transforms.size()
	for index in transforms.size():
		var transform: Transform3D = transforms[index]
		multi.set_instance_transform(index, transform.translated_local(offset))

	var instance := MultiMeshInstance3D.new()
	instance.name = node_name
	instance.multimesh = multi
	instance.material_override = material
	# `LightmapGI` walks `MeshInstance3D` and nothing else, so this can never be
	# baked. Dynamic is the honest setting: it takes indirect light from the
	# lightmap's probes. See `docs/adr_pending_182.md`.
	instance.gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC
	instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	return instance


# --- meshes ----------------------------------------------------------------
#
# Primitives rather than authored geometry, and that is a scope decision rather
# than a placeholder: this ticket is about *placement* and about the bake, and a
# modelled tree belongs with the asset work. Every mesh below is unit-height at
# unit scale so the instance transform is the only thing that sizes it.


func _trunk_mesh() -> Mesh:
	var trunk := CylinderMesh.new()
	trunk.top_radius = TREE_TRUNK_RADIUS * 0.7
	trunk.bottom_radius = TREE_TRUNK_RADIUS
	trunk.height = TREE_HEIGHT_LOW
	trunk.radial_segments = 6
	trunk.rings = 1
	# Origin at the base rather than the middle, so an instance sits on the ground
	# it was placed on instead of half in it.
	return _lifted(trunk, TREE_HEIGHT_LOW * 0.5)


func _canopy_mesh() -> Mesh:
	var canopy := SphereMesh.new()
	canopy.radius = TREE_HEIGHT_LOW * 0.31
	canopy.height = TREE_HEIGHT_LOW * 0.80
	canopy.radial_segments = 7
	canopy.rings = 4
	return _lifted(canopy, TREE_HEIGHT_LOW * 0.70)


func _shrub_mesh() -> Mesh:
	var shrub := SphereMesh.new()
	shrub.radius = 1.0
	shrub.height = 2.0
	shrub.radial_segments = 6
	shrub.rings = 3
	return _lifted(shrub, 0.75)


func _post_mesh() -> Mesh:
	var post := CylinderMesh.new()
	post.top_radius = POST_RADIUS
	post.bottom_radius = POST_RADIUS
	post.height = POST_HEIGHT
	post.radial_segments = 6
	post.rings = 1
	return _lifted(post, POST_HEIGHT * 0.5)


func _panel_mesh() -> Mesh:
	var panel := BoxMesh.new()
	panel.size = Vector3(0.62, 0.45, 0.05)
	return panel


func _board_mesh() -> Mesh:
	var board := BoxMesh.new()
	board.size = Vector3(0.80, 0.80, 0.05)
	return board


func _tire_mesh() -> Mesh:
	var tire := CylinderMesh.new()
	tire.top_radius = TIRE_RADIUS
	tire.bottom_radius = TIRE_RADIUS
	tire.height = TIRE_HEIGHT
	tire.radial_segments = 8
	tire.rings = 1
	return tire


## A primitive shifted along Y, baked into its arrays.
##
## `PrimitiveMesh` has no origin offset, and putting the offset on the instance
## transform instead would make it scale with the instance — a 12 m tree would
## have its base 1.5 m underground. Baking it into the mesh keeps the offset in
## the same space as the geometry it belongs to.
func _lifted(source: PrimitiveMesh, lift: float) -> ArrayMesh:
	var arrays := source.get_mesh_arrays()
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	for index in vertices.size():
		vertices[index] = vertices[index] + Vector3(0.0, lift, 0.0)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


# --- materials -------------------------------------------------------------
#
# Albedo as a real reflectance, the same rule `bake_test.gd` states: dry bark is
# around 0.10, summer broadleaf foliage around 0.09 in the visible, galvanized
# steel around 0.35. Physical light units are on project-wide, so a color pulled
# out of the air here shows up as a wrong number rather than as a picture someone
# has to judge.


func _bark_material() -> StandardMaterial3D:
	return _matte(Color(0.115, 0.092, 0.075), 0.92)


func _foliage_material() -> StandardMaterial3D:
	var material := _matte(Color(0.083, 0.115, 0.052), 0.95)
	# Two-sided, because a low-poly canopy is read from inside as often as from
	# outside once the camera is under the tree line.
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	return material


func _post_material() -> StandardMaterial3D:
	return _matte(Color(0.35, 0.36, 0.37), 0.45)


func _marshal_material() -> StandardMaterial3D:
	return _matte(Color(0.72, 0.28, 0.04), 0.60)


func _board_material() -> StandardMaterial3D:
	return _matte(Color(0.78, 0.78, 0.76), 0.70)


func _tire_material() -> StandardMaterial3D:
	return _matte(Color(0.045, 0.045, 0.047), 0.88)


func _matte(albedo: Color, roughness: float) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = albedo
	material.roughness = roughness
	material.metallic = 0.0
	return material
