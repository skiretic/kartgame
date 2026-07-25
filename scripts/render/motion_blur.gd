@tool
class_name MotionBlurEffect
extends CompositorEffect

## Per-pixel gather motion blur, as a Godot compositor effect.
##
## `ARCHITECTURE.md` §4 lists motion blur as non-optional and §19 names it as the
## risk worth surfacing early: Godot ships no motion blur, so the only route is a
## custom render pass, and the pass needs a velocity buffer the engine only
## produces on request.
##
## `needs_motion_vectors` is what makes it possible at all. Without it the engine
## never renders the velocity pass unless TAA or FSR2 happens to be on, and
## `get_velocity_layer()` hands back an invalid RID.
##
## **Why there are two dispatches for one effect.** A gather blur reads
## neighboring pixels while writing this one, so it cannot run in place. The
## obvious fix is to copy the color buffer aside first — but Godot's color
## buffer is created with `SAMPLING | COLOR_ATTACHMENT | STORAGE |
## INPUT_ATTACHMENT` and neither copy bit, so `texture_copy()` refuses it in both
## directions. What the buffer *can* do is be sampled and be written as a storage
## image, which is enough:
##
##   1. sample the color buffer, gather along velocity, write to a scratch
##      texture this effect owns;
##   2. sample the scratch texture, write it back over the color buffer.
##
## Same total bandwidth as a copy would have cost, one extra dispatch, and no
## dependency on usage bits the engine does not promise.
##
## **Where this sits in the frame.** Compositor effects run inside the 3D pass,
## before Godot's post-processing — so before the TAA resolve, not after it. That
## is not the textbook order (blur normally comes last, on a resolved image), and
## it is a property of the API rather than a choice made here. See
## `docs/DECISIONS.md` ADR-0019.

const SHADER_PATH := "res://scripts/render/motion_blur.glsl"

## Name the effect's scratch textures are registered under in the scene buffers.
## The engine drops the whole context when the buffers are reconfigured, which is
## what makes a window resize free rather than a leak.
const TEXTURE_CONTEXT := &"kartgame_motion_blur"
const SCRATCH_TEXTURE := &"blurred"

const GROUP_SIZE := 8
const PUSH_CONSTANT_BYTES := 32

enum DebugMode {
	## The blurred image.
	OFF = 0,
	## Velocity vectors, red for horizontal and green for vertical, mid gray at
	## rest. The first thing to look at when the blur does nothing: it separates
	## "the pass did not run" from "the engine wrote no motion vectors".
	VELOCITY = 1,
	## Blur length in pixels as a heat map, against `max_blur_pixels`.
	BLUR_LENGTH = 2,
	## Uncorrelated noise, redrawn every frame. Diagnostic only: it answers
	## whether anything temporal runs after this effect. See ADR-0019.
	FRAME_NOISE = 3,
}

## Degrees of shutter opening per frame. 180 is the film convention — the shutter
## is open for half of each frame, so half a frame of motion is smeared. Higher
## values blur more; 360 is a shutter that never closes.
@export_range(0.0, 360.0, 1.0) var shutter_angle_degrees := 180.0

## Upper bound on taps along the blur vector. The shader takes roughly one tap
## per two pixels of blur and stops here, so this caps the worst case rather than
## setting the usual one — most of the screen is far below it.
@export_range(4, 64, 1) var samples := 32

## Longest blur the pass will produce, in pixels. Bounds both the cost and the
## damage a single dropped frame can do.
@export_range(4.0, 256.0, 1.0) var max_blur_pixels := 64.0

@export var debug_mode: DebugMode = DebugMode.OFF

var _rd: RenderingDevice
var _shader: RID
var _pipeline: RID
var _sampler: RID
var _reported_buffers := false
var _frame_index := 0


func _init() -> void:
	# Post-transparent is the last callback the API offers, so it is the closest
	# available point to "the finished 3D image".
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	needs_motion_vectors = true
	# Hands the pass an MSAA-resolved color buffer rather than the raw samples.
	access_resolved_color = true
	RenderingServer.call_on_render_thread(_initialize_resources)


func _notification(what: int) -> void:
	if what != NOTIFICATION_PREDELETE:
		return
	# Freeing the shader frees the pipeline built from it; the sampler is
	# independent and has to go separately.
	if _shader.is_valid():
		_rd.free_rid(_shader)
	if _sampler.is_valid():
		_rd.free_rid(_sampler)


func _initialize_resources() -> void:
	_rd = RenderingServer.get_rendering_device()
	if _rd == null:
		# No rendering device means a headless run. Nothing to do and nothing
		# wrong; _render_callback will not be called either.
		return

	var shader_file: RDShaderFile = load(SHADER_PATH)
	if shader_file == null:
		push_error("MotionBlurEffect: could not load %s" % SHADER_PATH)
		return

	var spirv := shader_file.get_spirv()
	if not spirv.compile_error_compute.is_empty():
		push_error("MotionBlurEffect: %s" % spirv.compile_error_compute)
		return

	_shader = _rd.shader_create_from_spirv(spirv)
	if not _shader.is_valid():
		push_error("MotionBlurEffect: shader_create_from_spirv failed")
		return

	_pipeline = _rd.compute_pipeline_create(_shader)

	var state := RDSamplerState.new()
	state.mag_filter = RenderingDevice.SAMPLER_FILTER_LINEAR
	state.min_filter = RenderingDevice.SAMPLER_FILTER_LINEAR
	# Taps are clamped into range in the shader as well, but a clamped sampler
	# means a tap that lands exactly on the edge repeats the edge pixel instead
	# of wrapping the far side of the screen into frame.
	state.repeat_u = RenderingDevice.SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
	state.repeat_v = RenderingDevice.SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
	state.repeat_w = RenderingDevice.SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
	_sampler = _rd.sampler_create(state)


func _render_callback(callback_type: int, render_data: RenderData) -> void:
	if _rd == null or not _pipeline.is_valid():
		return
	if callback_type != effect_callback_type:
		return

	var buffers := render_data.get_render_scene_buffers() as RenderSceneBuffersRD
	if buffers == null:
		return

	var size := buffers.get_internal_size()
	if size.x == 0 or size.y == 0:
		return

	if not buffers.has_texture(TEXTURE_CONTEXT, SCRATCH_TEXTURE):
		buffers.create_texture(
			TEXTURE_CONTEXT,
			SCRATCH_TEXTURE,
			# Matches the engine's color buffer, which reports RGBA16F. The
			# shader's `layout(rgba16f)` image qualifier has to agree with both.
			RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT,
			RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT
				| RenderingDevice.TEXTURE_USAGE_STORAGE_BIT,
			RenderingDevice.TEXTURE_SAMPLES_1,
			size,
			buffers.get_view_count(),
			1,
			true,
			false,
		)

	var groups_x := int(ceili(size.x / float(GROUP_SIZE)))
	var groups_y := int(ceili(size.y / float(GROUP_SIZE)))
	_frame_index += 1
	var blur_constant := _push_constant(size, false)
	var blit_constant := _push_constant(size, true)

	_rd.draw_command_begin_label("kartgame motion blur", Color(0.9, 0.5, 0.2))

	for view in buffers.get_view_count():
		var color := buffers.get_color_layer(view, false)
		var velocity := buffers.get_velocity_layer(view, false)
		if not color.is_valid() or not velocity.is_valid():
			_report_once(color, velocity)
			continue

		var scratch := buffers.get_texture_slice(TEXTURE_CONTEXT, SCRATCH_TEXTURE, view, 0, 1, 1)

		# Pass 1: color + velocity -> scratch.
		_dispatch(
			UniformSetCacheRD.get_cache(_shader, 0, [
				_sampled(0, color), _sampled(1, velocity), _storage(2, scratch),
			]),
			blur_constant, groups_x, groups_y,
		)

		# Pass 2: scratch -> color. Separate compute lists, so the write to the
		# color buffer is ordered after every read of it in pass 1.
		_dispatch(
			UniformSetCacheRD.get_cache(_shader, 0, [
				_sampled(0, scratch), _sampled(1, velocity), _storage(2, color),
			]),
			blit_constant, groups_x, groups_y,
		)

	_rd.draw_command_end_label()


func _dispatch(uniform_set: RID, push_constant: PackedByteArray, x: int, y: int) -> void:
	var compute_list := _rd.compute_list_begin()
	_rd.compute_list_bind_compute_pipeline(compute_list, _pipeline)
	_rd.compute_list_bind_uniform_set(compute_list, uniform_set, 0)
	_rd.compute_list_set_push_constant(compute_list, push_constant, PUSH_CONSTANT_BYTES)
	_rd.compute_list_dispatch(compute_list, x, y, 1)
	_rd.compute_list_end()


func _push_constant(size: Vector2i, blit_only: bool) -> PackedByteArray:
	var bytes := PackedByteArray()
	bytes.resize(PUSH_CONSTANT_BYTES)
	bytes.encode_float(0, float(size.x))
	bytes.encode_float(4, float(size.y))
	bytes.encode_float(8, shutter_angle_degrees / 360.0)
	bytes.encode_float(12, max_blur_pixels)
	bytes.encode_s32(16, samples)
	bytes.encode_s32(20, int(debug_mode))
	bytes.encode_s32(24, 1 if blit_only else 0)
	bytes.encode_float(28, float(_frame_index))
	return bytes


func _sampled(binding: int, texture: RID) -> RDUniform:
	var uniform := RDUniform.new()
	uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
	uniform.binding = binding
	uniform.add_id(_sampler)
	uniform.add_id(texture)
	return uniform


func _storage(binding: int, texture: RID) -> RDUniform:
	var uniform := RDUniform.new()
	uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	uniform.binding = binding
	uniform.add_id(texture)
	return uniform


## Missing motion vectors are the failure this effect is most likely to hit and
## the hardest to read from a black screen, so say so — once, not every frame.
func _report_once(color: RID, velocity: RID) -> void:
	if _reported_buffers:
		return
	_reported_buffers = true
	push_warning(
		"MotionBlurEffect: skipping, color valid=%s velocity valid=%s. "
		% [color.is_valid(), velocity.is_valid()]
		+ "An invalid velocity RID means the engine rendered no motion vector pass."
	)
