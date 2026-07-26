class_name TrackRibbon
extends RefCounted

## Turns a `TrackLayout` centerline into geometry: road, curbs, edge lines.
##
## Everything is accumulated as a **triangle soup** — no index array — because the
## same vertex list then serves twice, once as an `ArrayMesh` and once as a
## `ConcavePolygonShape3D`. ARCHITECTURE.md §11 states that principle for the M5
## pipeline in one line: because both consumers read the same definition, what you
## see and what you collide with cannot drift apart. A `.tres` saving of a few
## hundred shared vertices is not worth giving that up, and at 1,030 m of track
## the whole thing is a few thousand triangles.
##
## ## Winding, which was measured rather than assumed
##
## Godot's front face is the one whose vertices wind so that `(b - a) x (c - a)`
## points **against** the surface normal. That is the opposite of the usual
## right-hand convention and it was read off `PlaneMesh.get_mesh_arrays()` rather
## than remembered: the shipped plane faces +Y and its two triangles both cross to
## (0, -1, 0). `add_quad()` takes the outward normal it wants and reorders to suit,
## so no caller has to hold this in its head.
##
## The reason to care is in CLAUDE.md's trap list: inverted winding is invisible
## when a material has backface culling off, and `build.box` and `build.lathe`
## were wound inward for two milestones with every render looking correct.

## Where the track surface sits relative to the grass, meters.
##
## It cannot be zero. Two coplanar collider faces make a suspension raycast's
## answer arbitrary along the whole boundary, which is the same argument
## `proving_ground.gd` makes for its grass lip — but the sign here is the other
## way round, and that is forced. The run-off is one large slab that also passes
## *under* the road, because a track-shaped hole in a field is a great deal of
## geometry for no gain, so the road has to win by standing above it.
##
## CIK-FIA Circuit Regulations Part 1 §7.5 asks for the opposite — verges must
## continue the track's transversal profile "with no negative slope between track
## and verge". 2 mm is not the lip that rule exists to prevent: against
## `chassis_flex.h`'s 277,500 N/m rear tire rate it is 555 N of transient, which
## the suspension absorbs the way it absorbs a painted edge. Anything larger and
## leaving the road would begin with a jump.
const ROAD_LIP := 0.002

## Track width, meters, constant the whole way round.
##
## **Not sourced.** The CIK-FIA circuit regulations put the minimum track width in
## Appendix 13, which is not in the Part 1 text `docs/REFERENCES.md` already
## records, so this is a chosen number and is flagged as one. What it is anchored
## to is the kart: FIA Karting Art. 8.1.1 caps overall width at 1,400 mm and this
## kart's rear track is that, so 8 m is 5.7 kart widths — enough for the widest
## line through the hairpin to be a real choice rather than a formality, and
## narrow enough that running out of road is a mistake with a consequence.
const TRACK_WIDTH := 8.0

## The white lines down both edges of the asphalt, meters wide.
##
## Sourced, unlike the width itself. CIK-FIA Circuit Regulations Part 1 §7.2: "The
## left and right edges of the track asphalt must be delimited by the required
## white or yellow lines ... but with a maximum width of 120 mm." 100 mm is inside
## that. They sit 4 mm above the road for the same reason `proving_ground.gd`'s
## paint does — coplanar paint z-fights across the whole track, which looks like a
## rendering bug and is one.
const EDGE_LINE_WIDTH := 0.10
const PAINT_LIFT := 0.004

## Curb width and the height of its vertical face above the asphalt, meters.
##
## `src/core/surface.h` is explicit that the grip number is not what makes a curb
## a curb — the geometry is — so the height is the number that matters here and it
## is bracketed from both sides:
##
##   * **Above** 12 mm, which is the ripple amplitude that same header defines. A
##     curb shorter than its own ripple is paint with a friction coefficient.
##   * **Below** 35 mm, which is where `proving_ground.gd` puts the underside of
##     the chassis collider — "the bottom of the frame rails rather than the lowest
##     thing on the kart". A curb taller than that is a wall that catches the floor
##     tray, and #42's acceptance is that a curb is *distinct* from asphalt and
##     grass, not that it ends the lap.
##
## 30 mm. The ripple itself is **not** modeled: surface.h says the solver must not
## add that displacement on top of a raycast, and the ripple lands with real
## geometry at M5.
const CURB_WIDTH := 1.00
const CURB_HEIGHT := 0.030

## How far a curb takes to rise to full height at each end, meters.
##
## Real curbs are ramped in, and so is this one, but only along the direction of
## travel. The **lateral** face stays vertical at full height for the whole curb,
## because that face is the one #139 wants driven at: "a wheel driven at it at
## 100+ km/h does not pass through it". A driver who meets the end of a curb
## head-on at 140 km/h is meeting a 30 mm step at zero degrees of incidence, which
## is the one case that is a modeling artifact rather than a test.
const CURB_RAMP := 4.0

## How far a curb reaches below the surface, meters. It is not a slab lying on the
## grass, it is the top of something buried, and the depth only has to be more
## than the 2 mm the road stands proud by.
const CURB_DEPTH := 0.15

var _vertices := PackedVector3Array()
var _normals := PackedVector3Array()
var _uvs := PackedVector2Array()


# --- accumulation ----------------------------------------------------------


## Four corners in order around the quad, four UVs to match, and the direction the
## face should point. Split into two triangles, wound to Godot's convention.
func add_quad(
	a: Vector3, b: Vector3, c: Vector3, d: Vector3,
	uv_a: Vector2, uv_b: Vector2, uv_c: Vector2, uv_d: Vector2,
	facing: Vector3
) -> void:
	add_triangle(a, b, c, uv_a, uv_b, uv_c, facing)
	add_triangle(a, c, d, uv_a, uv_c, uv_d, facing)


func add_triangle(
	a: Vector3, b: Vector3, c: Vector3,
	uv_a: Vector2, uv_b: Vector2, uv_c: Vector2,
	facing: Vector3
) -> void:
	var cross := (b - a).cross(c - a)
	# A ramped curb ends in a zero-height wall, and a zero-area triangle has no
	# normal to reason about. Dropped rather than emitted: it renders as nothing
	# and it is one more degenerate face for the collision cooker to think about.
	if cross.length_squared() < 1e-12:
		return
	if cross.dot(facing) > 0.0:
		# Wound the wrong way for Godot, which wants the cross product *against*
		# the normal. See the header — this was measured, not recalled.
		var swap := b
		b = c
		c = swap
		var swap_uv := uv_b
		uv_b = uv_c
		uv_c = swap_uv
	_vertices.append(a)
	_vertices.append(b)
	_vertices.append(c)
	for _index in 3:
		_normals.append(facing)
	_uvs.append(uv_a)
	_uvs.append(uv_b)
	_uvs.append(uv_c)


func triangle_count() -> int:
	return _vertices.size() / 3


func mesh() -> ArrayMesh:
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = _vertices
	arrays[Mesh.ARRAY_NORMAL] = _normals
	arrays[Mesh.ARRAY_TEX_UV] = _uvs
	var built := ArrayMesh.new()
	built.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return built


## The same triangles again, for `ConcavePolygonShape3D.set_faces()`.
func faces() -> PackedVector3Array:
	return _vertices


# --- the pieces ------------------------------------------------------------


## The asphalt: one quad per sample gap, edge to edge.
##
## UVs are in **meters** — u across the track, v along it — which is what makes
## `LookEnv.asphalt_material(1.0)` the right call at the other end. That helper
## sets `uv1_scale` to `extent / 4.00 m`, so an extent of one turns it into a
## straight 1/4.00, and the §5 texel-density standard comes out right without this
## file knowing what the standard is.
func add_road(samples: Array[Dictionary]) -> void:
	var half := TRACK_WIDTH * 0.5
	_add_strip(samples, -half, half, ROAD_LIP, ROAD_LIP, Vector3.UP)


## The two edge lines, as one mesh.
##
## Inboard of the asphalt edge rather than overhanging it, so that the painted
## line is the last thing with grip on it and a wheel on the white is still a
## wheel on the track.
func add_edge_lines(samples: Array[Dictionary]) -> void:
	var half := TRACK_WIDTH * 0.5
	var y := ROAD_LIP + PAINT_LIFT
	_add_strip(samples, -half, -half + EDGE_LINE_WIDTH, y, y, Vector3.UP)
	_add_strip(samples, half - EDGE_LINE_WIDTH, half, y, y, Vector3.UP)


## A curb outboard of one edge of the road, ramped in at both ends.
##
## `hand` is -1 for the left of the direction of travel and +1 for the right, so
## an apex curb is on the same side the corner turns.
func add_curb(samples: Array[Dictionary], hand: float) -> void:
	if samples.size() < 2:
		return
	var half := TRACK_WIDTH * 0.5
	var inner := hand * half
	var outer := hand * (half + CURB_WIDTH)
	var first: float = samples[0]["distance"]
	var last: float = samples[samples.size() - 1]["distance"]

	for index in range(samples.size() - 1):
		var near: Dictionary = samples[index]
		var far: Dictionary = samples[index + 1]
		var y_near := ROAD_LIP + CURB_HEIGHT * _ramp(near["distance"], first, last)
		var y_far := ROAD_LIP + CURB_HEIGHT * _ramp(far["distance"], first, last)

		var inner_near := _point(near, inner, y_near)
		var inner_far := _point(far, inner, y_far)
		var outer_near := _point(near, outer, y_near)
		var outer_far := _point(far, outer, y_far)
		var uv_near := Vector2(0.0, near["distance"])
		var uv_far := Vector2(CURB_WIDTH, far["distance"])

		# The top face.
		add_quad(
			inner_near, outer_near, outer_far, inner_far,
			Vector2(0.0, uv_near.y), Vector2(CURB_WIDTH, uv_near.y),
			Vector2(CURB_WIDTH, uv_far.y), Vector2(0.0, uv_far.y),
			Vector3.UP
		)

		# The two vertical faces, dropping to `CURB_DEPTH` below the grass. The
		# inner one is the face a wheel climbs and the whole reason this is a curb
		# rather than a painted strip; the outer one is mostly buried and is here
		# so the curb reads as a solid object from a chase camera at the apex.
		var facing_inner := TrackLayout.right(near["heading"]) * -hand
		var facing_outer := TrackLayout.right(near["heading"]) * hand
		_add_wall(near, far, inner, y_near, y_far, facing_inner)
		_add_wall(near, far, outer, y_near, y_far, facing_outer)


## A rectangle of paint lying on the road, centered on a centerline sample.
##
## Used for the start/finish line and the grid box. Built here rather than as a
## `BoxMesh` node so it follows the road's heading exactly and cannot end up
## slightly crooked on a corner.
func add_paint_bar(
	sample: Dictionary, along: float, from_offset: float, to_offset: float
) -> void:
	var heading: float = sample["heading"]
	var position: Vector3 = sample["position"]
	var forward := TrackLayout.forward(heading)
	var lateral := TrackLayout.right(heading)
	var y := ROAD_LIP + PAINT_LIFT
	var back := position - forward * along * 0.5
	var front := position + forward * along * 0.5
	var lift := Vector3(0.0, y, 0.0)
	add_quad(
		back + lateral * from_offset + lift,
		back + lateral * to_offset + lift,
		front + lateral * to_offset + lift,
		front + lateral * from_offset + lift,
		Vector2(0.0, 0.0), Vector2(1.0, 0.0), Vector2(1.0, 1.0), Vector2(0.0, 1.0),
		Vector3.UP
	)


# --- internals -------------------------------------------------------------


func _add_strip(
	samples: Array[Dictionary],
	from_offset: float, to_offset: float,
	from_y: float, to_y: float,
	facing: Vector3
) -> void:
	for index in range(samples.size() - 1):
		var near: Dictionary = samples[index]
		var far: Dictionary = samples[index + 1]
		add_quad(
			_point(near, from_offset, from_y),
			_point(near, to_offset, to_y),
			_point(far, to_offset, to_y),
			_point(far, from_offset, from_y),
			Vector2(from_offset, near["distance"]),
			Vector2(to_offset, near["distance"]),
			Vector2(to_offset, far["distance"]),
			Vector2(from_offset, far["distance"]),
			facing
		)


func _add_wall(
	near: Dictionary, far: Dictionary,
	offset: float, near_top: float, far_top: float,
	facing: Vector3
) -> void:
	add_quad(
		_point(near, offset, -CURB_DEPTH),
		_point(near, offset, near_top),
		_point(far, offset, far_top),
		_point(far, offset, -CURB_DEPTH),
		Vector2(0.0, near["distance"]),
		Vector2(CURB_HEIGHT, near["distance"]),
		Vector2(CURB_HEIGHT, far["distance"]),
		Vector2(0.0, far["distance"]),
		facing
	)


## Zero at either end of a curb, one in the middle.
func _ramp(distance: float, first: float, last: float) -> float:
	var from_start := distance - first
	var from_end := last - distance
	return clampf(minf(from_start, from_end) / CURB_RAMP, 0.0, 1.0)


static func _point(sample: Dictionary, offset: float, y: float) -> Vector3:
	var position: Vector3 = sample["position"]
	return position + TrackLayout.right(sample["heading"]) * offset + Vector3(0.0, y, 0.0)


# --- materials -------------------------------------------------------------


## Red and white, alternating every meter along the curb.
##
## CIK-FIA Circuit Regulations Part 1 §14.6, already recorded in
## `docs/REFERENCES.md` as the source for `surface.h`'s curb grip number: "Kerbs
## must be painted in two colours alternately (recommended colours: red and
## white)." That is not decoration here — it is the only thing on the track that
## tells a driver at 140 km/h where the asphalt stops, and a curb the same color
## as the road is a curb nobody aims at.
##
## The stripe is a four-pixel texture rather than vertex colors because the curb's
## v coordinate is already distance in meters, so tiling it at 0.5 puts one
## red-white pair every 2 m with no per-vertex work at all.
static func curb_material() -> StandardMaterial3D:
	var image := Image.create(4, 4, false, Image.FORMAT_RGB8)
	for y in 4:
		var stripe := Color(0.62, 0.09, 0.09) if y < 2 else Color(0.88, 0.87, 0.85)
		for x in 4:
			image.set_pixel(x, y, stripe)
	var material := StandardMaterial3D.new()
	material.albedo_texture = ImageTexture.create_from_image(image)
	# Nearest, or a four-pixel texture blurs into one pink curb.
	material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST_WITH_MIPMAPS
	material.roughness = 0.75
	material.metallic = 0.0
	material.uv1_scale = Vector3(1.0, 0.5, 1.0)
	return material


## Track paint: the edge lines, the start line and the grid box.
static func paint_material() -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.86, 0.86, 0.84)
	material.roughness = 0.85
	material.metallic = 0.0
	return material


## A 2 m grass tile, generated rather than loaded.
##
## Duplicated from `proving_ground.gd`'s `_grass_texture()` on purpose and with
## regret: that function is in a file this work does not own, and two scenes now
## want the same turf. It belongs here, or in `LookEnv` beside the photoscans,
## with the proving ground calling it — see the note this work reports back.
##
## Same rule either way: fixed seed, so the texture is identical every run.
## Nothing in a scene the determinism harness may one day drive on gets to consult
## a clock or an unseeded generator.
static func grass_material() -> StandardMaterial3D:
	const TEXTURE_SIZE := 128
	var image := Image.create(TEXTURE_SIZE, TEXTURE_SIZE, false, Image.FORMAT_RGB8)
	var noise := RandomNumberGenerator.new()
	noise.seed = 0x67726173  # "gras"
	for y in TEXTURE_SIZE:
		for x in TEXTURE_SIZE:
			var clump := sin(float(x) * 0.19) * 0.5 + sin(float(y) * 0.11) * 0.5
			var value := 0.5 + clump * 0.16 + noise.randf_range(-0.20, 0.20)
			image.set_pixel(x, y, Color(
				clampf(0.055 + value * 0.075, 0.0, 1.0),
				clampf(0.130 + value * 0.190, 0.0, 1.0),
				clampf(0.030 + value * 0.055, 0.0, 1.0)
			))
	var material := StandardMaterial3D.new()
	material.albedo_texture = ImageTexture.create_from_image(image)
	material.roughness = 0.95
	material.metallic = 0.0
	return material
