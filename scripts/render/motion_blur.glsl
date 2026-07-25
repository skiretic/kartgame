#[compute]
#version 450

// Per-pixel gather motion blur.
//
// Reads Godot's velocity buffer, which stores each pixel's screen-space UV
// displacement since the previous rendered frame, and integrates the colour
// along that vector. `shutter` scales the frame's motion down to the fraction of
// the frame a real shutter is actually open for: a 180 degree shutter angle is
// open for half the frame, so it smears half a frame of motion.
//
// The gather is centred on the pixel rather than trailing behind it. A physical
// shutter closes at the frame time and so blurs only backwards, but a backwards
// blur reads as lag on a camera that is following the action. Centring costs
// nothing here because a symmetric gather does not care about the sign of the
// velocity vector.
//
// Known limitation, and the reason the tile pass exists in film-quality
// implementations: a gather can only collect colour that is already on screen at
// this pixel. A fast object against a still background therefore blurs *inside*
// its own silhouette and stops dead at its edge, instead of bleeding out over
// the background the way a real exposure does. Camera motion — which is most of
// what a racing game shows — has no such problem, because every pixel on screen
// is moving.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_colour;
layout(set = 0, binding = 1) uniform sampler2D source_velocity;
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image2D dest_colour;

layout(push_constant, std430) uniform Params {
	vec2 raster_size;
	float shutter;
	float max_blur_pixels;
	int samples;
	int debug_mode;
	// Second pass: copy source to destination and nothing else. See the header
	// comment in motion_blur.gd for why the write-back needs a pass of its own.
	int blit_only;
	float frame_index;
} params;

const int DEBUG_OFF = 0;
const int DEBUG_VELOCITY = 1;
const int DEBUG_BLUR_LENGTH = 2;
const int DEBUG_FRAME_NOISE = 3;

// Below half a pixel of travel there is nothing to integrate and the taps would
// all land in the same texel, so the whole loop is skipped.
const float MINIMUM_BLUR_PIXELS = 0.5;

// Interleaved gradient noise. Offsetting each pixel's taps by a fraction of the
// tap spacing turns the banding a fixed tap pattern produces into noise, which
// TAA and the eye both handle far better than stepped ghosts.
float dither(vec2 pixel) {
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.raster_size);
	if (pixel.x >= size.x || pixel.y >= size.y) {
		return;
	}

	vec2 uv = (vec2(pixel) + 0.5) / params.raster_size;

	if (params.blit_only != 0) {
		imageStore(dest_colour, pixel, texture(source_colour, uv));
		return;
	}

	vec2 velocity_uv = texture(source_velocity, uv).xy * params.shutter;
	vec2 velocity_pixels = velocity_uv * params.raster_size;
	float blur_pixels = length(velocity_pixels);

	// A single dropped frame produces an enormous velocity vector. Without a
	// clamp that turns into a full-screen smear, which reads as a bug rather
	// than as motion.
	if (blur_pixels > params.max_blur_pixels) {
		velocity_uv *= params.max_blur_pixels / blur_pixels;
		blur_pixels = params.max_blur_pixels;
	}

	if (params.debug_mode == DEBUG_VELOCITY) {
		vec2 normalised = velocity_pixels / max(params.max_blur_pixels, 1.0);
		imageStore(dest_colour, pixel, vec4(normalised * 0.5 + 0.5, 0.5, 1.0));
		return;
	}
	// Writes uncorrelated noise every frame. If any temporal pass ran *after*
	// this effect it would average the noise towards flat grey; if this effect
	// is the last thing to touch the colour buffer, the noise survives to the
	// screen. That is the whole experiment behind ADR-0019.
	if (params.debug_mode == DEBUG_FRAME_NOISE) {
		float n = fract(sin(dot(
			vec2(pixel) + params.frame_index * 17.0, vec2(12.9898, 78.233)
		)) * 43758.5453);
		imageStore(dest_colour, pixel, vec4(vec3(n), 1.0));
		return;
	}
	if (params.debug_mode == DEBUG_BLUR_LENGTH) {
		float heat = clamp(blur_pixels / max(params.max_blur_pixels, 1.0), 0.0, 1.0);
		imageStore(dest_colour, pixel, vec4(heat, heat * heat, 0.0, 1.0));
		return;
	}

	if (blur_pixels < MINIMUM_BLUR_PIXELS) {
		imageStore(dest_colour, pixel, texture(source_colour, uv));
		return;
	}

	// Tap count follows the blur length rather than being fixed. A 3 px blur
	// integrated with 32 taps samples the same texel repeatedly and buys
	// nothing; a 60 px blur with 16 taps leaves 4 px between taps, which a hard
	// edge turns into sixteen visible ghosts. Roughly one tap per two pixels
	// keeps the tap spacing under what the display can resolve, and makes the
	// cost proportional to how much blur is actually being asked for.
	int taps = clamp(int(blur_pixels * 0.5) + 1, 4, max(params.samples, 4));
	float jitter = dither(vec2(pixel)) - 0.5;
	vec4 accumulated = vec4(0.0);

	for (int i = 0; i < taps; i++) {
		// (i + 0.5) / taps spans (0, 1); minus 0.5 centres it on the pixel. The
		// jitter is one tap wide, so consecutive taps stay ordered along the
		// vector and only the phase moves.
		float t = (float(i) + 0.5 + jitter) / float(taps) - 0.5;
		vec2 tap_uv = clamp(uv - velocity_uv * t, vec2(0.0), vec2(1.0));
		accumulated += texture(source_colour, tap_uv);
	}

	imageStore(dest_colour, pixel, accumulated / float(taps));
}
