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
const EYE_OFFSET := Vector3(-2.35, 1.28, 3.05)
const LOOK_OFFSET := Vector3(0.0, 0.42, 0.15)
const FOV_DEGREES := 42.0

## Enough to frame a kart and the grid it stands on, and no more. The circuit
## mesh is 400 m across and mostly out of shot on purpose.
const FLAT_GROUND := Color(0.062745, 0.094118, 0.125490)

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
	camera.fov = FOV_DEGREES
	add_child(camera)

	if _mode == "flat":
		_build_flat()
	else:
		_build_grid(args)
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
	_add_mesh(KART_MESH, pole, "Kart", "genkart.sh")

	camera.global_transform = Transform3D(Basis(), pole * EYE_OFFSET)
	camera.look_at(pole * LOOK_OFFSET, Vector3.UP)


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
