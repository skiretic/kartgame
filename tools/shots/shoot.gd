extends SceneTree

## Renders a scene to a PNG and exits. The look-dev feedback loop.
##
##     tools/shots/shoot.sh --scene=res://scenes/look/lookdev.tscn --out=shot.png
##
## Why a script and not a screenshot key: judging a render needs the same frame
## under the same parameters every time. A still taken by hand is taken at a
## different moment with a different camera and proves nothing about the change
## that came before it. This runs the scene, waits a fixed number of frames, and
## writes the image — so two stills differ only where the code differs.
##
## It renders to a real window rather than headlessly on purpose. `--headless`
## has no rendering device at all, so there is nothing to capture; and on this
## host the headless *editor* path crashes outright (ADR-0018). A visible window
## is the only way to get pixels out of Godot on macOS today.
##
## ## Turntable mode
##
##     tools/shots/shoot.sh --scene=res://scenes/look/kartview.tscn \
##                          --turntable=8 --resolution=640x640
##
## `--turntable=N` captures N views into one contact sheet instead of one still.
## The scene decides what a view *is*, by implementing:
##
##     func set_turntable_view(index: int, count: int) -> void
##
## which is duck-typed, so scenes without it are unaffected. A contact sheet
## rather than N files on purpose: the point of issue #22 is that generated
## geometry gets *looked at* while it is being built, and eight angles in one
## image is one glance instead of eight. It is also a single artifact to hash,
## which makes the turntable a regression check as well as a review aid.
##
## Everything else — the double import, the fixed settle, the nominal-frame-rate
## stepping — is shared with the single-still path rather than reimplemented,
## which is the lesson ADR-0018 records about workarounds that get duplicated.

const DEFAULT_SETTLE_FRAMES := 32

## Frames at the end of the run to average render time over. The first frames of
## any run include pipeline compilation and buffer allocation and are not
## representative of anything.
const TIMED_FRAMES := 16

var _out_path: String
var _settle_frames: int
var _frames_seen := 0
var _scene_root: Node
var _measure := false
var _frame_times: Array[float] = []
var _gpu_samples: Array[float] = []
var _cpu_samples: Array[float] = []

## 0 means a single still, which is the original behavior and the default.
var _views := 0
var _view_index := 0
var _frames_in_view := 0
var _sheet: Image
var _sheet_columns := 0


func _initialize() -> void:
	var args := Cmdline.parse()

	var scene_path := Cmdline.as_string(args, "scene", "")
	if scene_path.is_empty():
		push_error("shoot.gd: --scene=res://path/to.tscn is required")
		quit(2)
		return

	_out_path = Cmdline.as_string(args, "out", "shot.png")
	# TAA and SSIL are temporal: they converge over frames rather than arriving
	# complete. Capturing frame 1 measures the accumulation buffer, not the look.
	_settle_frames = Cmdline.as_int(args, "settle", DEFAULT_SETTLE_FRAMES)

	var packed: PackedScene = load(scene_path)
	if packed == null:
		push_error("shoot.gd: could not load %s" % scene_path)
		quit(2)
		return

	_scene_root = packed.instantiate()
	root.add_child(_scene_root)

	# Frame time is only a measurement if nothing is throttling it. The scene
	# advances by a fixed nominal step rather than by the real delta, so
	# unlocking the frame rate changes what is measured without changing what is
	# rendered — the still is identical either way.
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0

	# ARCHITECTURE.md §15 sets a 10 ms rendering budget, so a shot that also
	# reports its cost is worth having. Off by default: on this host, enabling
	# render-time measurement hangs the process before the first frame — Godot's
	# Metal backend and GPU timestamp queries, not anything in this project. Ask
	# for it explicitly with --timing=true and expect it to work on Linux.
	_measure = Cmdline.as_bool(args, "timing", false)
	if _measure:
		root.set_measure_render_time(true)

	_views = maxi(Cmdline.as_int(args, "turntable", 0), 0)
	if _views > 0:
		if not _scene_root.has_method("set_turntable_view"):
			push_error("shoot.gd: --turntable needs the scene to implement set_turntable_view(index, count); %s does not" % scene_path)
			quit(2)
			return
		# Columns before rows, squarest first: eight views read best as 3x3 with a
		# gap than as 8x1, and a wide strip is unusable on a normal display.
		_sheet_columns = int(ceil(sqrt(float(_views))))
		_scene_root.set_turntable_view(0, _views)


func _process(delta: float) -> bool:
	_frames_seen += 1
	_frames_in_view += 1

	if _frames_in_view > _settle_frames - TIMED_FRAMES:
		_frame_times.append(delta * 1000.0)
		if _measure:
			_gpu_samples.append(root.get_measured_render_time_gpu())
			_cpu_samples.append(root.get_measured_render_time_cpu())

	if _frames_in_view < _settle_frames:
		return false

	# get_texture() returns the viewport's last completed frame, so this reads a
	# settled frame rather than a half-drawn one.
	var image := root.get_texture().get_image()

	if _views <= 0:
		return _save(image, _frames_seen)

	_blit_into_sheet(image)
	_view_index += 1
	if _view_index >= _views:
		return _save(_sheet, _frames_seen)

	# TAA and SSIL are temporal, so a camera that jumps to a new angle carries
	# stale history into the new view. Resetting the per-view frame counter gives
	# each view the full settle budget rather than only the first one.
	_frames_in_view = 0
	_scene_root.set_turntable_view(_view_index, _views)
	return false


## Place one view into the contact sheet, creating it on the first call.
##
## The sheet is allocated from the first captured image rather than from the
## requested resolution, because a viewport can be rounded by the display server
## and a mismatch would silently crop every cell.
func _blit_into_sheet(image: Image) -> void:
	var cell := Vector2i(image.get_width(), image.get_height())
	if _sheet == null:
		var rows := int(ceil(float(_views) / float(_sheet_columns)))
		_sheet = Image.create(
			cell.x * _sheet_columns, cell.y * rows, false, image.get_format())
		# Cells beyond the last view stay this color, which makes an incomplete
		# sheet obvious rather than looking like black geometry.
		_sheet.fill(Color(0.08, 0.08, 0.09))

	var column := _view_index % _sheet_columns
	var row := _view_index / _sheet_columns
	_sheet.blit_rect(
		image, Rect2i(Vector2i.ZERO, cell), Vector2i(column * cell.x, row * cell.y))


func _save(image: Image, frames: int) -> bool:
	var directory := _out_path.get_base_dir()
	if not directory.is_empty():
		DirAccess.make_dir_recursive_absolute(directory)

	var error := image.save_png(_out_path)
	if error != OK:
		push_error("shoot.gd: save_png(%s) failed with %d" % [_out_path, error])
		quit(1)
		return true

	var timing := "  frame %.2f ms" % _median(_frame_times)
	if _measure:
		timing += "  gpu %.2f ms  cpu %.2f ms" % [_median(_gpu_samples), _median(_cpu_samples)]
	var views := "" if _views <= 0 else "  %d views" % _views
	print("shot: %s (%dx%d, after %d frames)%s%s" % [
		_out_path, image.get_width(), image.get_height(), frames, views, timing,
	])
	return true


## Median rather than mean: a single scheduling hiccup in a 16-frame window
## moves a mean enough to read as a regression.
func _median(samples: Array[float]) -> float:
	if samples.is_empty():
		return 0.0
	var sorted := samples.duplicate()
	sorted.sort()
	return sorted[sorted.size() / 2]
