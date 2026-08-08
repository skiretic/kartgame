class_name ShellBackdrop
extends Node3D

## What is behind the menus. ADR-0053 §1: the paddock persists behind every
## screen, so this is built once and never rebuilt by a transition.
##
##     --backdrop=grid     the Valdirone start line, camera parked on the grid
##     --backdrop=flat     a solid field and nothing else
##
## **`flat` is not a debug mode, it is the gate's mode.** A fresh worktree has no
## `assets/generated/` — it is gitignored, reproduced by `gentrack.sh` and
## `genkart.sh` — so a shell probe that needed the `.glb`s could not run in the
## place agents actually work. `flat` has no asset dependency at all, which is
## what lets `shell_probe.gd` be a structural gate rather than an integration
## test wearing one.
##
## `grid` uses the meshes that already exist and adds no Blender work: #188's
## generated paddock drops into this same slot later, replacing `_build_grid()`
## and nothing else.
##
## The camera is **parked**, not a free cam and not an orbit. A menu backdrop
## that moves competes with the menu for the eye, and every still in `shots/` has
## to be reproducible from the command that made it.

const TRACK_JSON := "res://data/tracks/valdirone_nuova.track.json"
const TRACK_MESH := "res://assets/generated/valdirone_nuova.glb"
const KART_MESH := "res://assets/generated/kart.glb"

## Where the shell camera sits relative to the pole-position grid slot, in the
## kart's own frame: up and behind its left shoulder, looking at the airbox.
## Three-quarter front is the angle every kart photograph in `docs/REFERENCES.md`
## is taken from, and it is the one that reads as a machine rather than a
## silhouette.
##
## **The distance is set by how much of the frame the kart may occupy, not by
## taste.** The paddock's two cards flank the kart at the left and right edges,
## so the middle third of the screen has to hold the whole machine with air
## around it. At 6.62 m and a 42 deg vertical FOV the visible height is
## `2 * 6.62 * tan(21 deg)` = 5.08 m, and a 1.15 m kart is 23% of it. The first
## build sat at 4.05 m, which put the kart at 37% of frame height with its nose
## cropped and both cards lying across the bodywork.
const EYE_OFFSET := Vector3(-3.60, 1.90, 5.20)
const LOOK_OFFSET := Vector3(0.0, 0.45, 0.10)
const FOV_DEGREES := 42.0

## The flat backdrop's ground, **from the theme rather than copied out of it**.
##
## This was `Color(0.062745, 0.094118, 0.125490)` — `ShellTheme.SCR_GROUND`
## transcribed component for component — which is precisely the thing
## `shell_probe.gd` check 9 exists to stop: the livery round moves one constant
## and the field behind every menu silently keeps the old color. The check missed
## it because it scans `screens/` and `widgets/` and this file is in neither.
const FLAT_GROUND := ShellTheme.SCR_GROUND

var camera: Camera3D
var _mode := "grid"
var _report := PackedStringArray()


func _init() -> void:
	name = "Backdrop"


## `args` is the merged command line, so `--backdrop=` reaches here the same way
## every other scene argument does.
func build(args: Dictionary) -> void:
	_mode = Cmdline.as_string(args, "backdrop", "grid")

	camera = Camera3D.new()
	camera.name = "ShellCamera"
	add_child(camera)

	if _mode == "flat":
		_build_flat()
	else:
		_build_grid(args)

	# **Last, and after whatever the mode did to the camera.** Measured on 4.7.1:
	# assigning a `CameraAttributesPhysical` *overwrites* `fov`, `near` and `far`
	# from its own frustum, and the default 35 mm focal length is **37.8493
	# degrees**. So a camera set to 42 and then handed attributes renders at 37.85
	# and the constant above is dead. This file assigned them in that order, and
	# every paddock frame shipped 4.15 degrees tighter than the framing derivation
	# in the header solves for — a kart at 25.4% of frame height rather than 23%.
	# Assigning `fov` afterwards sticks, which is the whole fix.
	camera.fov = FOV_DEGREES
	camera.current = true


## What was actually built, as lines. Printed by `ShellRoot` and by the gate — a
## still shot against a backdrop that silently fell back to `flat` has to be
## diagnosable from its own log, which is the `SKIP_IMPORT=1` lesson applied to
## one more asset.
func report() -> PackedStringArray:
	return _report


func mode() -> String:
	return _mode


# --- flat ---------------------------------------------------------------------


## No `.glb`, no `.track.json`, no lightmap, no environment probe. A `WorldEnvironment`
## with a flat background and one light, so a screen composited over it is lit the
## same way every run and the gate has nothing to fetch.
func _build_flat() -> void:
	var world := WorldEnvironment.new()
	world.name = "FlatEnv"
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = FLAT_GROUND
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = FLAT_GROUND
	env.ambient_light_energy = 1.0
	world.environment = env
	add_child(world)

	camera.transform = Transform3D(Basis(), Vector3(0.0, 1.4, 4.0))
	camera.look_at(Vector3.ZERO, Vector3.UP)
	_report.append("backdrop flat: no generated asset touched")


# --- the grid -----------------------------------------------------------------


func _build_grid(args: Dictionary) -> void:
	# `look_env.gd` and nothing hand-rolled. Physical light units are on
	# project-wide and four ambient double-counting bugs have turned up so far;
	# CLAUDE.md's rule is that lighting lives in one place and a new scene calls
	# it rather than building its own.
	var world := WorldEnvironment.new()
	world.name = "ShellEnv"
	world.environment = LookEnv.environment(args)
	add_child(world)
	add_child(LookEnv.sun(args))
	camera.attributes = LookEnv.camera_attributes(args)

	var pole := Transform3D(Basis(), Vector3.ZERO)
	var track := KartTrack.new()
	if track.load(TRACK_JSON) == OK:
		track.select_layout(Cmdline.as_string(args, "layout", "forward"))
		if track.grid_count() > 0:
			pole = track.grid_transform(0)
			_report.append("backdrop grid: %s, pole slot at %.1f m" % [
				track.track_name(), track.length(),
			])
	else:
		_report.append("backdrop grid: %s did not load, kart at the origin" % TRACK_JSON)

	_add_mesh(TRACK_MESH, Transform3D(), "TrackMesh", "gentrack.sh")
	# **On the road, not on the grid transform.** `grid_transform()` returns a
	# *spawn* pose — it carries a lift so a physics kart dropped onto the grid
	# settles instead of starting interpenetrated, and `KartRig.SPAWN_LIFT` is its
	# default argument. The backdrop's kart is a static mesh that never falls, so
	# it keeps the lift forever: measured at 93 mm above the built surface, which
	# reads exactly as a kart hovering.
	#
	# Dropped onto the mesh that is actually drawn rather than onto
	# `KartTrack.sample()`, because those two are not the same surface. The
	# sampler returns the centerline and the pole slot is 3 m to the side, where
	# crossfall has taken the asphalt lower; trusting it would swap a 93 mm error
	# for a 33 mm one and still float.
	pole = _dropped_to_surface(pole)
	_add_mesh(KART_MESH, pole, "Kart", "genkart.sh")
	_add_reflection_probe()

	camera.global_transform = Transform3D(Basis(), pole * EYE_OFFSET)
	camera.look_at(pole * LOOK_OFFSET, Vector3.UP)


## The half of reflection Godot does well, and the one thing this backdrop was
## missing that `circuit.gd` has always had.
##
## ## What it was worth, measured
##
## The paddock and `valdirone.tscn` were shot from the identical eye and look
## point at 1280x720. The kart read a mean luma of **86.49** here against
## **118.80** in the circuit scene, and the asphalt **108.70** against **125.17** —
## the same sun, the same `LookEnv.environment`, the same physical exposure, and
## the kart 27% darker. Re-shooting the circuit with `--probe=false` gave **86.95**
## and **108.71**: the backdrop's own numbers to half a percent. So the probe is
## the whole difference, it is worth +36.6% on the kart and +15.1% on the asphalt,
## and nothing else about the two lighting rigs diverges.
##
## ## Sized over the drawn mesh rather than over what the camera can see
##
## `look_env.gd` sets `AMBIENT_DISABLED`, so a probe adds specular and no ambient
## and cannot double-count — that is the first of the three M1 bugs and the reason
## the helper exists. What a probe *can* do is put a hard rectangular seam across
## a broad rough surface at its own boundary, which is what `circuit.gd` learned
## when it sized one over the driving area instead of the world. The union of the
## track mesh's own AABBs is that world here, and `get_aabb()` is a stored bound
## per instance rather than a walk of the triangles.
##
## Skipped, quietly, when the mesh is absent: a paddock with no track to reflect
## has nothing for a probe to capture.
func _add_reflection_probe() -> void:
	var mesh := get_node_or_null("TrackMesh") as Node3D
	if mesh == null:
		return
	var bounds := AABB()
	var started := false
	for node: Node in mesh.find_children("*", "MeshInstance3D", true, false):
		var instance := node as MeshInstance3D
		if instance.mesh == null:
			continue
		var box := instance.global_transform * instance.get_aabb()
		bounds = box if not started else bounds.merge(box)
		started = true
	if not started:
		return
	var center := bounds.get_center()
	var extent := maxf(bounds.size.x, bounds.size.z)
	add_child(LookEnv.reflection_probe(
		Vector3(extent, maxf(bounds.size.y, 40.0) + 40.0, extent),
		Vector3(center.x, center.y + 20.0, center.z),
		extent))
	_report.append("reflection probe %.0f m over the track mesh" % extent)


## Sit a pose down on the track mesh that is actually being drawn.
##
## ## Why this is triangle arithmetic and not a physics drop
##
## `circuit_probe.gd` settles a real kart at 40 stations — drop from
## `KartRig.SPAWN_LIFT`, 120 ticks, four wheels on asphalt — and that is the right
## answer inside a session. It is not available here: `shell_probe.gd` check 9b
## asserts that **no `KartBody`, `PlayerDriver`, `SessionRunner`, `KartTuning` or
## `KartLapTimer` exists anywhere under `ShellRoot`**, which is GAMEDESIGN §9 as
## an assertion and the property M6's determinism harness depends on. A menu
## backdrop that ran a vehicle solver to place a decorative kart would break it.
##
## ## Why not the nearest vertex
##
## Tried, and it finds nothing. The asphalt ribbon is sampled every few meters, so
## a search radius small enough to be meaningful contains no vertices at all and a
## radius large enough to contain some is averaging across the crown. The surface
## under a point is a **triangle**, so the point has to be located inside one and
## the height interpolated across it.
##
## Returns the pose unchanged, and says so, when nothing is under it — a kart at
## its spawn height is a much better failure than a kart at the bottom of the
## world.
func _dropped_to_surface(pose: Transform3D) -> Transform3D:
	var mesh := get_node_or_null("TrackMesh") as Node3D
	if mesh == null:
		return pose
	var target := Vector2(pose.origin.x, pose.origin.z)
	var best := -INF
	for node: Node in mesh.find_children("*", "MeshInstance3D", true, false):
		var instance := node as MeshInstance3D
		if instance.mesh == null:
			continue
		var to_world := instance.global_transform
		for surface: int in instance.mesh.get_surface_count():
			var arrays: Array = instance.mesh.surface_get_arrays(surface)
			var points: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
			if indices.is_empty():
				continue
			for i: int in range(0, indices.size(), 3):
				var a: Vector3 = to_world * points[indices[i]]
				var b: Vector3 = to_world * points[indices[i + 1]]
				var c: Vector3 = to_world * points[indices[i + 2]]
				var height := _height_in_triangle(target, a, b, c)
				if height != INF:
					best = maxf(best, height)
	if best == -INF:
		_report.append("kart left at its spawn pose: no track surface under %.1f, %.1f"
				% [target.x, target.y])
		return pose
	var dropped := pose
	dropped.origin.y = best
	_report.append("kart dropped %.0f mm onto the built surface"
			% ((pose.origin.y - best) * 1000.0))
	return dropped


## The y of a triangle at a point in plan, or `INF` when the point is outside it.
##
## Barycentric, in the x/z plane. The degenerate guard is a triangle seen edge-on
## from above — a vertical wall, of which the barriers are full — where the
## denominator is zero and every point would otherwise read as inside.
static func _height_in_triangle(at: Vector2, a: Vector3, b: Vector3, c: Vector3) -> float:
	var pa := Vector2(a.x, a.z)
	var pb := Vector2(b.x, b.z)
	var pc := Vector2(c.x, c.z)
	var denominator := (pb.y - pc.y) * (pa.x - pc.x) + (pc.x - pb.x) * (pa.y - pc.y)
	if absf(denominator) < 1e-9:
		return INF
	var u := ((pb.y - pc.y) * (at.x - pc.x) + (pc.x - pb.x) * (at.y - pc.y)) / denominator
	var v := ((pc.y - pa.y) * (at.x - pc.x) + (pa.x - pc.x) * (at.y - pc.y)) / denominator
	var w := 1.0 - u - v
	# A hair of tolerance so a point exactly on a shared edge belongs to one of
	# the two triangles rather than to neither.
	if u < -1e-6 or v < -1e-6 or w < -1e-6:
		return INF
	return u * a.y + v * b.y + w * c.y


## Loads a generated mesh, or says in one sentence which command produces it.
##
## Not fatal. A missing `.glb` should leave a menu you can still read and drive
## from — the shell's whole job — rather than a black screen, and the boot
## screen's check line is where a player sees it. ADR-0052.
func _add_mesh(path: String, at: Transform3D, node_name: String, command: String) -> void:
	if not ResourceLoader.exists(path):
		_report.append("%s absent -- run tools/blender/%s" % [path, command])
		return
	var packed := load(path) as PackedScene
	if packed == null:
		_report.append("%s is not a scene" % path)
		return
	var node := packed.instantiate() as Node3D
	node.name = node_name
	node.transform = at
	add_child(node)
	_report.append("%s loaded" % path)
