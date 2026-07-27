class_name GhostKart
extends Node3D

## The ghost on track: your own best lap, drawn beside you.
##
## ROADMAP M3c, `GAMEDESIGN.md` §3, ADR-0041. `src/session/kart_ghost.h` is the
## recording and the playback; this is the thing you see.
##
## ## It is a visual and nothing else
##
## No `RigidBody3D`, no `CollisionShape3D`, no `KartBody`, no `_physics_process` —
## **grep this file for `_physics_process` and there is none, deliberately.** A
## ghost is not re-simulated. ADR-0041 is explicit that re-simulating one buys
## nothing and costs a second vehicle solve every tick, and a ghost that could be
## hit would turn a time trial against yourself into a collision with yourself.
##
## The transform is set in `_process`, at frame rate, which is the honest place for
## it: the ghost's motion is interpolated from a 30 Hz stream, so sampling it at
## 120 Hz would produce four identical frames out of every four and cost the same.
## Feeding it from `_process` also means it physically cannot enter the physics
## path — there is nothing to be careless about later.
##
## ## Two ways to drive it, and the runner uses the first
##
##     ghost.playback_time = t            an owner drives the clock
##     ghost.autoplay = true              the node advances its own clock and loops
##
## `autoplay` exists for a still and for this file's own probe. A session runner
## sets `playback_time` from its own lap clock so that the ghost and the driver are
## on one clock and the delta on the HUD means what it says.
##
## The clock ends at `KartGhost.playback_duration()`, not at its lap time. The last
## sample sits up to one sample interval short of the line — 33 ms, 0.67 m at
## 20 m/s — and `ghost_sample_at` clamps rather than extrapolating, so looping on
## the lap time would freeze the ghost at the line for that fraction and then jump
## it. `kart_ghost.h` says why closing that gap needs something `ghost.h` does not
## have.

## The generated kart, which is the same mesh the player is driving.
##
## Reusing it rather than a simplified stand-in is the reason the ghost reads as a
## kart at speed at all: it *is* the kart, on the real racing line, at the real
## attitude the recording caught it at.
const KART_MESH_PATH := "res://assets/generated/kart.glb"

## What makes the ghost obviously not a rival.
##
## Three properties, and each one is doing a job:
##
##   * **Unshaded.** The ghost's brightness never tracks the light, so it cannot be
##     mistaken for a second kart on track at a glance, in any lighting, on any
##     circuit. This is the one that carries the distinction; the color alone would
##     not, because a real kart can be any color.
##   * **Translucent.** Says "not solid" without hiding the line it is driving, and
##     it is the line a driver is actually reading.
##   * **Casts no shadow, receives none.** A shadow is the strongest cue in the
##     frame that something is really there.
##
## Cyan because nothing in `look_env.gd`'s sky or `track_ribbon.gd`'s asphalt, curb
## and marking palette is near it. The visual design is not this file's call —
## these four constants are the whole of it, and each is one line to change.
const GHOST_TINT := Color(0.20, 0.80, 1.00)
const GHOST_ALPHA := 0.40
const GHOST_UNSHADED := true
const GHOST_CASTS_SHADOW := false

## Advance the clock from `_process` and loop, rather than being driven by an owner.
@export var autoplay := false

## Where in the lap the ghost is, in seconds. Clamped to the stream on read-back,
## so an owner may hand it a live lap time longer than the ghost's own lap without
## checking first.
@export var playback_time := 0.0

var _ghost: KartGhost = null
var _mesh_root: Node3D = null
var _material: StandardMaterial3D = null


func _ready() -> void:
	# Hidden until there is something to draw. A ghost kart parked at the origin on
	# the start line is worse than no ghost: it reads as a stalled rival.
	visible = false
	_material = _build_material()
	_mesh_root = _load_mesh()
	if _mesh_root != null:
		_apply_material(_mesh_root)


## Hand it a recorded lap. `null`, or a ghost that is not complete, hides it.
##
## Takes a `KartGhost` rather than a path so that whoever owns the profile decides
## which ghost this is and whether it was comparable — `KartGhost.compare_to_session`
## is that call, and it belongs to the runner, not to the thing drawing the mesh.
func set_ghost(ghost: KartGhost) -> void:
	_ghost = ghost
	playback_time = 0.0
	var drawable := ghost != null and ghost.is_complete()
	visible = drawable and _mesh_root != null
	if drawable and _mesh_root != null:
		# Placed immediately rather than on the next frame. A ghost that appears at
		# the origin for one frame before jumping to the start line is a flash of a
		# kart in the scenery, and it happens on every lap.
		_place(playback_time)


func has_ghost() -> bool:
	return _ghost != null and _ghost.is_complete()


## The length of the stream, which is what a caller loops on. Zero with no ghost.
func playback_duration() -> float:
	return _ghost.playback_duration() if has_ghost() else 0.0


func _process(delta: float) -> void:
	if not has_ghost():
		return
	if autoplay:
		var span := _ghost.playback_duration()
		playback_time = fposmod(playback_time + delta, span) if span > 0.0 else 0.0
	_place(playback_time)


func _place(time_s: float) -> void:
	# `transform_at_time` clamps at both ends, so no guard here is missing — a time
	# past the end holds the last sample rather than extrapolating a kart off the
	# end of the circuit.
	transform = _ghost.transform_at_time(time_s)


func _load_mesh() -> Node3D:
	if not ResourceLoader.exists(KART_MESH_PATH):
		# Not an error. A ghost with no mesh is a missing feature; a ghost that
		# stopped the scene loading would take the driving with it.
		push_warning("GhostKart: no kart at %s — run tools/blender/genkart.sh" % KART_MESH_PATH)
		return null
	var instance := (load(KART_MESH_PATH) as PackedScene).instantiate() as Node3D
	instance.name = "GhostMesh"
	add_child(instance)
	return instance


func _build_material() -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(GHOST_TINT.r, GHOST_TINT.g, GHOST_TINT.b, GHOST_ALPHA)
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	if GHOST_UNSHADED:
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	# Sorted back-to-front against itself, so the far side of the bodywork does not
	# draw over the near side. A translucent kart is a stack of surfaces and without
	# this the ghost looks inside-out from half the angles.
	material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_OPAQUE_ONLY
	return material


## One override material over every surface of the mesh, wheels and driver included.
##
## `material_override` rather than editing the imported materials: the mesh is a
## shared resource and the player's kart is the same `.glb`, so touching its
## materials would tint the kart the driver is sitting in.
func _apply_material(root: Node3D) -> void:
	for node in _descendants(root):
		var visual := node as GeometryInstance3D
		if visual == null:
			continue
		visual.material_override = _material
		if not GHOST_CASTS_SHADOW:
			visual.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF


func _descendants(root: Node) -> Array[Node]:
	var found: Array[Node] = []
	var pending: Array[Node] = [root]
	while not pending.is_empty():
		var node: Node = pending.pop_back()
		found.append(node)
		for child in node.get_children():
			pending.append(child)
	return found
