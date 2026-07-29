class_name TrackTerrain
extends RefCounted

## Ground that follows the circuit's elevation instead of lying flat under it.
## Issue #182, and it is a prerequisite for the scatter rather than a look item.
##
## ## What was wrong before
##
## `circuit.gd` put one flat `PlaneMesh` under the whole world at the circuit's
## lowest point. That is fine while the only thing standing on it is the road,
## because the road stands on its own spline — but Valdirone climbs **12.55 m**,
## so at the high point the asphalt, its verge and its run-off are a ribbon
## hanging 12.05 m in the air over a lawn, and the very first prop placed beside
## the track is a tree with its roots in the sky. Nothing about scatter can be
## judged until the ground under it is real.
##
## ## The height field
##
## One number per grid vertex, from the track and nothing else:
##
##     station, gap = track.project(x, z)         nearest point on the centerline
##     reserved     = corridor.reserved_at(station)
##     road         = track.sample(station).elevation
##     h            = road - SHOULDER_DROP                        gap <= reserved
##                    lerp(road - SHOULDER_DROP, floor, s)        beyond it
##     s            = smoothstep(0, 1, (gap - reserved) / FALLOFF)
##
## So the ground is a shoulder just under the built circuit, everywhere, and it
## relaxes to the base plane over `FALLOFF` meters of open ground. Nothing chooses
## a landform: the circuit is the landform, which is the same argument ADR-0046
## makes for furniture.
##
## ## Why it is smoothed afterwards
##
## `project` returns the *nearest* station, so where the lap passes close to
## itself at two different heights the nearest-station answer flips across one
## grid cell and the field has a cliff in it. Valdirone does exactly that — the
## design's own closest approach is a little over 6 m of clear ground plus both
## half-widths. `SMOOTHING_PASSES` Jacobi passes over the field fix it, with every
## vertex inside a corridor **pinned**, so the shoulder cannot be dragged up
## through the road it is supposed to sit under.
##
## ## Collision
##
## The surface is a `ConcavePolygonShape3D` with `backface_collision`, exactly as
## the road is and for the same reason. `circuit.gd` keeps its original box slab
## underneath as the backstop it always was — sunk `BASE_CLEARANCE` so the two are
## never coplanar, which is the condition that makes a suspension raycast's answer
## arbitrary along a whole boundary.

## How far under the road's own elevation the shoulder sits, meters.
##
## It cannot be zero: the run-off apron is built at exactly road level and two
## coplanar collider faces make a wheel raycast arbitrary. It should not be large
## either — every meter of it is a step down at the outer edge of the run-off. A
## quarter of a meter reads as a graded shoulder and is 125x the 2 mm lip
## `TrackRibbon` already relies on for the same purpose.
const SHOULDER_DROP := 0.25

## Meters of open ground over which the shoulder relaxes to the base plane.
##
## Sized from the worst case rather than chosen: the road is 12.05 m above the
## base at the top of the climb, so 90 m is a 13.4% average grade there and less
## everywhere else. Halving it would put a 27% bank round the high side of the
## circuit, which reads as a quarry rather than as Italian hill country.
const FALLOFF := 90.0

## Jacobi passes over the raw field, corridor vertices pinned.
##
## Swept on Valdirone at a 5 m cell, worst cell-to-cell step in meters:
##
##     passes    0      4      8     24
##     step   7.672  2.382  2.343  2.343
##
## Converged by 8 and four is within 39 mm of it, which is why four. The residual
## 2.34 m is **not** something more passes can reach: it is between two *pinned*
## vertices, where the lap passes close to itself at two different heights and the
## nearest-station answer flips. A 2.3 m bank over a 5 m cell between two levels
## of a circuit that climbs 12.55 m is the landform, not an artifact. Without any
## smoothing the same field has a 7.67 m cliff in it, which is.
const SMOOTHING_PASSES := 4

## How far the base box slab is sunk below the field's own base level, meters.
## Purely to keep two large horizontal colliders out of the same plane.
const BASE_CLEARANCE := 1.0

## Lightmap texels per meter of ground, before `LightmapGI.texel_scale`.
##
## Indirect light on open ground is about as low-frequency as light gets — no
## creases, no contact, one shadow term from the sun — so 1.6 texels/m is a
## 0.63 m texel and there is nothing at that scale for it to miss. It is also what
## keeps the ground off the atlas budget: 640 m square at this density is one
## 1024x1024 slice, and at 4 texels/m it would be 2560 and over
## `max_texture_size`.
const LIGHTMAP_TEXELS_PER_METER := 1.6

## Meters per grass tile. `TrackRibbon.grass_material` generates a 128 px texture
## with no scale of its own, and `circuit.gd`'s flat plane tiled it at 2 m.
## Unchanged, so the ground reads the same size as it did before.
const GRASS_TILE := 2.0

var _cell := 5.0
var _cells := 0
var _origin := Vector2.ZERO
var _floor := 0.0
var _height := PackedFloat32Array()


## Builds the field. `p_extent` is the side of the square, `p_centre` its middle
## in world XZ, `p_floor_y` the base plane the old flat ground sat at.
func build(track: KartTrack, corridor: TrackCorridor, p_extent: float, p_centre: Vector3,
		p_floor_y: float, p_cell: float, p_passes: int = SMOOTHING_PASSES) -> void:
	_cell = maxf(p_cell, 1.0)
	_cells = maxi(2, int(ceil(p_extent / _cell)))
	_origin = Vector2(p_centre.x - p_extent * 0.5, p_centre.z - p_extent * 0.5)
	_floor = p_floor_y

	var count := (_cells + 1) * (_cells + 1)
	_height.resize(count)
	# Pinned vertices are the ones inside a corridor. Recorded during the raw pass
	# rather than recomputed, because the test costs a `project` and there are as
	# many of them as there are vertices.
	var pinned := PackedByteArray()
	pinned.resize(count)

	for row in _cells + 1:
		for column in _cells + 1:
			var index := row * (_cells + 1) + column
			var x := _origin.x + float(column) * _cell
			var z := _origin.y + float(row) * _cell
			var placed := track.project(Vector3(x, 0.0, z))
			var station: float = placed["distance"]
			var gap: float = placed["gap"]
			var road: float = float(track.sample(station)["elevation"]) - SHOULDER_DROP
			var reserved := corridor.reserved_at(station)
			if gap <= reserved:
				_height[index] = road
				pinned[index] = 1
			else:
				var blend := smoothstep(0.0, 1.0, (gap - reserved) / FALLOFF)
				_height[index] = lerpf(road, _floor, blend)

	_smooth(pinned, p_passes)


## The height of the built surface at a point, exactly.
##
## Exactly, not approximately: the scatter puts objects on this and a bilinear
## read of a field drawn as triangles would sink or float every prop by up to half
## a cell's relief. The cell is split along its (0,0)-(1,1) diagonal here and in
## `visual()`, and the two have to stay that way together.
func height_at(x: float, z: float) -> float:
	if _height.is_empty():
		return _floor
	var fx := clampf((x - _origin.x) / _cell, 0.0, float(_cells) - 0.0001)
	var fz := clampf((z - _origin.y) / _cell, 0.0, float(_cells) - 0.0001)
	var column := int(fx)
	var row := int(fz)
	var u := fx - float(column)
	var v := fz - float(row)
	var h00 := _at(row, column)
	var h10 := _at(row, column + 1)
	var h01 := _at(row + 1, column)
	var h11 := _at(row + 1, column + 1)
	if u >= v:
		return h00 + (h10 - h00) * u + (h11 - h10) * v
	return h00 + (h11 - h01) * u + (h01 - h00) * v


## The worst height step between two neighboring vertices, meters. The number the
## smoothing pass exists to bring down, printed rather than assumed.
func roughness() -> float:
	var worst := 0.0
	for row in _cells + 1:
		for column in _cells:
			worst = maxf(worst, absf(_at(row, column + 1) - _at(row, column)))
	for row in _cells:
		for column in _cells + 1:
			worst = maxf(worst, absf(_at(row + 1, column) - _at(row, column)))
	return worst


func vertex_count() -> int:
	return _height.size()


func triangle_count() -> int:
	return _cells * _cells * 2


func base_y() -> float:
	return _floor - BASE_CLEARANCE


## The drawn surface. Normals come off the field by central difference rather
## than from face averaging — the field is a height map, so the analytic normal is
## both cheaper and smoother than anything reconstructed from the triangles.
func visual() -> MeshInstance3D:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	# UV2 is the lightmap channel, and a height field is the one surface that gets
	# it for free: it is a function of (x, z), so the plan projection is a
	# guaranteed non-overlapping unwrap of the whole thing into the unit square at
	# uniform density. `gentrack.py` has to snake a 98:1 ribbon into ten rows to
	# reach the same place; this needs two divisions.
	var uv2s := PackedVector2Array()
	vertices.resize(_height.size())
	normals.resize(_height.size())
	uvs.resize(_height.size())
	uv2s.resize(_height.size())

	for row in _cells + 1:
		for column in _cells + 1:
			var index := row * (_cells + 1) + column
			var x := _origin.x + float(column) * _cell
			var z := _origin.y + float(row) * _cell
			vertices[index] = Vector3(x, _height[index], z)
			var dx := (_at(row, column + 1) - _at(row, column - 1)) / (2.0 * _cell)
			var dz := (_at(row + 1, column) - _at(row - 1, column)) / (2.0 * _cell)
			normals[index] = Vector3(-dx, 1.0, -dz).normalized()
			uvs[index] = Vector2(x, z) / GRASS_TILE
			uv2s[index] = Vector2(float(column), float(row)) / float(_cells)

	var indices := PackedInt32Array()
	indices.resize(_cells * _cells * 6)
	var write := 0
	for row in _cells:
		for column in _cells:
			var a := row * (_cells + 1) + column
			var b := a + 1
			var c := a + _cells + 1
			var d := c + 1
			# Godot's front face winds so that (b - a) x (c - a) points *against*
			# the surface normal — `TrackRibbon`'s header measured that off
			# `PlaneMesh` rather than remembering it, and this order is the one
			# that comes out facing up.
			indices[write] = a
			indices[write + 1] = c
			indices[write + 2] = b
			indices[write + 3] = b
			indices[write + 4] = c
			indices[write + 5] = d
			write += 6

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_TEX_UV2] = uv2s
	arrays[Mesh.ARRAY_INDEX] = indices

	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	# `LightmapGI` sizes every mesh from this and **silently bakes at 64x64 when it
	# is zero**, whatever the mesh's real size — which on 640 m of ground is one
	# texel per ten meters with no visible error anywhere. `preflight.gd` warns
	# about exactly this; the number is set here rather than left to it.
	mesh.lightmap_size_hint = Vector2i(_lightmap_side(), _lightmap_side())

	var instance := MeshInstance3D.new()
	instance.name = "GroundVisual"
	instance.mesh = mesh
	instance.material_override = TrackRibbon.grass_material()
	return instance


## The lightmap side, in texels, at `LIGHTMAP_TEXELS_PER_METER`.
##
## Rounded up to a multiple of 64 and **not** to a power of two: the atlas packer
## has no such requirement, and rounding 1,040 up to 2,048 quadrupled the ground's
## slice — 4.2 Mtexel of a 4.7 Mtexel scene — for nothing.
func _lightmap_side() -> int:
	var meters := float(_cells) * _cell
	var wanted := maxi(64, int(ceil(meters * LIGHTMAP_TEXELS_PER_METER)))
	return mini(int(ceil(float(wanted) / 64.0)) * 64, 4096)


## The same triangles as a collider. One definition, two consumers, locally.
func body(surface_type: int) -> StaticBody3D:
	var faces := PackedVector3Array()
	faces.resize(_cells * _cells * 6)
	var write := 0
	for row in _cells:
		for column in _cells:
			var a := _corner(row, column)
			var b := _corner(row, column + 1)
			var c := _corner(row + 1, column)
			var d := _corner(row + 1, column + 1)
			faces[write] = a
			faces[write + 1] = c
			faces[write + 2] = b
			faces[write + 3] = b
			faces[write + 4] = c
			faces[write + 5] = d
			write += 6

	var static_body := StaticBody3D.new()
	static_body.name = "GroundTerrain"
	# Friction is the tire model's, not this material's — ADR-0033, the combine
	# rule is measured to be min(a, b) and `KartBody::_ready` sets its own body to
	# zero. The number that matters here is the surface type.
	var physics_material := PhysicsMaterial.new()
	physics_material.friction = 1.0
	physics_material.rough = true
	static_body.physics_material_override = physics_material
	static_body.set_meta("surface_type", surface_type)

	var shape := ConcavePolygonShape3D.new()
	shape.set_faces(faces)
	# Two-sided for the road's reason: a surface rather than a volume has one
	# failure mode a slab does not, which is that anything ending up underneath it
	# falls out of the world.
	shape.backface_collision = true
	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.name = "GroundTerrainShape"
	static_body.add_child(collider)
	return static_body


# --- internals -------------------------------------------------------------


## Jacobi smoothing with the corridor held fixed.
##
## Held fixed matters more than the smoothing does. An unpinned pass drags the
## shoulder upward wherever the ground beside it is higher, and a shoulder that
## rises through the run-off apron is a hill the kart collides with on the racing
## line — a scatter change that moved a lap time, which is the one outcome this
## work is not allowed to produce.
func _smooth(pinned: PackedByteArray, passes: int) -> void:
	var side := _cells + 1
	for _pass in passes:
		var next := _height.duplicate()
		for row in side:
			for column in side:
				var index := row * side + column
				if pinned[index] == 1:
					continue
				next[index] = (
					_at(row, column - 1) + _at(row, column + 1)
					+ _at(row - 1, column) + _at(row + 1, column)
				) * 0.25
		_height = next


## Clamped at the edge, so the smoothing kernel and the normal's central
## difference both work on the border without a special case.
func _at(row: int, column: int) -> float:
	var side := _cells + 1
	var r := clampi(row, 0, side - 1)
	var c := clampi(column, 0, side - 1)
	return _height[r * side + c]


func _corner(row: int, column: int) -> Vector3:
	return Vector3(
		_origin.x + float(column) * _cell,
		_at(row, column),
		_origin.y + float(row) * _cell
	)
