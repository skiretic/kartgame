#include "audio/noise_voice.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <chrono>

using namespace godot;

namespace kartgame {

namespace {

int64_t now_ns() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count();
}

} // namespace

// --- NoiseVoiceStream -------------------------------------------------------

void NoiseVoiceStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_layer", "layer"), &NoiseVoiceStream::set_layer);
	ClassDB::bind_method(D_METHOD("get_layer"), &NoiseVoiceStream::get_layer);
	ClassDB::bind_method(D_METHOD("set_gain", "gain"), &NoiseVoiceStream::set_gain);
	ClassDB::bind_method(D_METHOD("get_gain"), &NoiseVoiceStream::get_gain);
	ClassDB::bind_method(D_METHOD("voice_stats"), &NoiseVoiceStream::voice_stats);
	ClassDB::bind_method(D_METHOD("reset_stats"), &NoiseVoiceStream::reset_stats);

	BIND_ENUM_CONSTANT(LAYER_SCRUB);
	BIND_ENUM_CONSTANT(LAYER_WIND);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "layer", PROPERTY_HINT_ENUM, "Scrub,Wind"),
			"set_layer", "get_layer");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"),
			"set_gain", "get_gain");

	// The property names are the `core/tuning.h` keys verbatim, so the rig forwards
	// a changed tunable with `stream.set(key, value)` and no lookup table. A rename
	// on either side breaks it loudly at the one call site rather than quietly
	// everywhere.
	ClassDB::bind_method(D_METHOD("set_scrub_center_hz", "hz"), &NoiseVoiceStream::set_scrub_center_hz);
	ClassDB::bind_method(D_METHOD("get_scrub_center_hz"), &NoiseVoiceStream::get_scrub_center_hz);
	ClassDB::bind_method(D_METHOD("set_scrub_q", "q"), &NoiseVoiceStream::set_scrub_q);
	ClassDB::bind_method(D_METHOD("get_scrub_q"), &NoiseVoiceStream::get_scrub_q);
	ClassDB::bind_method(D_METHOD("set_scrub_gamma", "gamma"), &NoiseVoiceStream::set_scrub_gamma);
	ClassDB::bind_method(D_METHOD("get_scrub_gamma"), &NoiseVoiceStream::get_scrub_gamma);
	ClassDB::bind_method(D_METHOD("set_scrub_full_speed", "ms"), &NoiseVoiceStream::set_scrub_full_speed);
	ClassDB::bind_method(D_METHOD("get_scrub_full_speed"), &NoiseVoiceStream::get_scrub_full_speed);
	ClassDB::bind_method(D_METHOD("set_wind_cutoff_per_ms", "hz_per_ms"),
			&NoiseVoiceStream::set_wind_cutoff_per_ms);
	ClassDB::bind_method(D_METHOD("get_wind_cutoff_per_ms"), &NoiseVoiceStream::get_wind_cutoff_per_ms);
	ClassDB::bind_method(D_METHOD("set_wind_speed_exponent", "exponent"),
			&NoiseVoiceStream::set_wind_speed_exponent);
	ClassDB::bind_method(D_METHOD("get_wind_speed_exponent"), &NoiseVoiceStream::get_wind_speed_exponent);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scrub_center_hz"), "set_scrub_center_hz", "get_scrub_center_hz");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scrub_q"), "set_scrub_q", "get_scrub_q");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scrub_gamma"), "set_scrub_gamma", "get_scrub_gamma");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scrub_full_speed"), "set_scrub_full_speed", "get_scrub_full_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_cutoff_per_ms"), "set_wind_cutoff_per_ms", "get_wind_cutoff_per_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_speed_exponent"), "set_wind_speed_exponent", "get_wind_speed_exponent");
}

void NoiseVoiceStream::publish(const kart::core::EngineAudioInput &p_input) {
	_state.publish(p_input);
}

bool NoiseVoiceStream::read(kart::core::EngineAudioInput &r_input) const {
	return _state.read(r_input);
}

void NoiseVoiceStream::set_layer(int p_layer) {
	// Clamped rather than trusted. An out-of-range value would otherwise fall through
	// the render branch as "not wind" and silently produce a scrub layer on a stream
	// the scene mounted as something else, which is a defect that only shows up as
	// wind being audible from another kart's position.
	const int32_t layer = (p_layer == LAYER_WIND) ? LAYER_WIND : LAYER_SCRUB;
	_layer.store(layer, std::memory_order_relaxed);
}

int NoiseVoiceStream::get_layer() const {
	return static_cast<int>(_layer.load(std::memory_order_relaxed));
}

void NoiseVoiceStream::set_gain(double p_gain) {
	_gain.store(p_gain < 0.0 ? 0.0 : (p_gain > 1.0 ? 1.0 : p_gain), std::memory_order_relaxed);
}

double NoiseVoiceStream::get_gain() const {
	return _gain.load(std::memory_order_relaxed);
}

// The six spectral knobs. Clamped to the same ranges `core/tuning.h` declares, so
// a caller that bypasses the registry -- a scene, a test, a probe -- cannot put the
// filter somewhere the arithmetic does not survive. A cutoff at zero and a Q at
// zero are both divisions by zero one layer down.
void NoiseVoiceStream::set_scrub_center_hz(double p_hz) {
	_scrub_center_hz.store(p_hz < 20.0 ? 20.0 : (p_hz > 12000.0 ? 12000.0 : p_hz),
			std::memory_order_relaxed);
}

double NoiseVoiceStream::get_scrub_center_hz() const {
	return _scrub_center_hz.load(std::memory_order_relaxed);
}

void NoiseVoiceStream::set_scrub_q(double p_q) {
	_scrub_q.store(p_q < 0.05 ? 0.05 : (p_q > 40.0 ? 40.0 : p_q), std::memory_order_relaxed);
}

double NoiseVoiceStream::get_scrub_q() const {
	return _scrub_q.load(std::memory_order_relaxed);
}

void NoiseVoiceStream::set_scrub_gamma(double p_gamma) {
	_scrub_gamma.store(p_gamma < 0.05 ? 0.05 : (p_gamma > 8.0 ? 8.0 : p_gamma),
			std::memory_order_relaxed);
}

double NoiseVoiceStream::get_scrub_gamma() const {
	return _scrub_gamma.load(std::memory_order_relaxed);
}

void NoiseVoiceStream::set_scrub_full_speed(double p_ms) {
	_scrub_full_speed.store(p_ms < 0.5 ? 0.5 : (p_ms > 100.0 ? 100.0 : p_ms),
			std::memory_order_relaxed);
}

double NoiseVoiceStream::get_scrub_full_speed() const {
	return _scrub_full_speed.load(std::memory_order_relaxed);
}

void NoiseVoiceStream::set_wind_cutoff_per_ms(double p_hz_per_ms) {
	_wind_cutoff_per_ms.store(p_hz_per_ms < 0.0 ? 0.0 : (p_hz_per_ms > 400.0 ? 400.0 : p_hz_per_ms),
			std::memory_order_relaxed);
}

double NoiseVoiceStream::get_wind_cutoff_per_ms() const {
	return _wind_cutoff_per_ms.load(std::memory_order_relaxed);
}

void NoiseVoiceStream::set_wind_speed_exponent(double p_exponent) {
	_wind_speed_exponent.store(p_exponent < 0.1 ? 0.1 : (p_exponent > 8.0 ? 8.0 : p_exponent),
			std::memory_order_relaxed);
}

double NoiseVoiceStream::get_wind_speed_exponent() const {
	return _wind_speed_exponent.load(std::memory_order_relaxed);
}

kart::core::ScrubWindConfig NoiseVoiceStream::tuning_config() const {
	kart::core::ScrubWindConfig config;
	const double gain = _gain.load(std::memory_order_relaxed);
	config.scrub_gain = gain;
	config.wind_gain = gain;
	config.scrub_center_hz = _scrub_center_hz.load(std::memory_order_relaxed);
	config.scrub_q = _scrub_q.load(std::memory_order_relaxed);
	config.scrub_gamma = _scrub_gamma.load(std::memory_order_relaxed);
	config.scrub_full_speed_ms = _scrub_full_speed.load(std::memory_order_relaxed);
	config.wind_cutoff_hz_per_ms = _wind_cutoff_per_ms.load(std::memory_order_relaxed);
	config.wind_speed_exponent = _wind_speed_exponent.load(std::memory_order_relaxed);
	return config;
}

Dictionary NoiseVoiceStream::voice_stats() const {
	Dictionary d;
	const int64_t frames = _render_frames_total.load(std::memory_order_relaxed);
	const int64_t total_ns = _render_ns_total.load(std::memory_order_relaxed);
	const int64_t worst_ns = _render_ns_worst.load(std::memory_order_relaxed);
	const int32_t worst_frames = _worst_block_frames.load(std::memory_order_relaxed);
	const double rate = _mix_rate.load(std::memory_order_relaxed);

	d["mix_calls"] = _mix_calls.load(std::memory_order_relaxed);
	d["layer"] = get_layer();
	d["mix_rate"] = rate;
	d["seq_gave_up"] = _state.gave_up();
	d["seq_retries"] = _state.retries();

	// The level the layer settled at, published from the audio thread. This is the
	// HUD row that makes "scrub signals grip loss before the visuals do" checkable
	// while driving rather than only by ear — #84's first acceptance criterion.
	d["level"] = _level.load(std::memory_order_relaxed);

	// Per frame, because the block size is the device's choice and the frame period
	// is the deadline's unit.
	d["render_ns_per_frame"] = frames > 0
			? static_cast<double>(total_ns) / static_cast<double>(frames)
			: 0.0;
	d["render_ns_worst_per_frame"] = worst_frames > 0
			? static_cast<double>(worst_ns) / static_cast<double>(worst_frames)
			: 0.0;

	const double frame_period_ns = rate > 0.0 ? 1.0e9 / rate : 0.0;
	if (frame_period_ns > 0.0) {
		d["load_fraction"] = frames > 0
				? (static_cast<double>(total_ns) / static_cast<double>(frames)) / frame_period_ns
				: 0.0;
		d["load_fraction_worst"] = worst_frames > 0
				? (static_cast<double>(worst_ns) / static_cast<double>(worst_frames)) / frame_period_ns
				: 0.0;
	} else {
		d["load_fraction"] = 0.0;
		d["load_fraction_worst"] = 0.0;
	}
	return d;
}

void NoiseVoiceStream::reset_stats() {
	_mix_calls.store(0, std::memory_order_relaxed);
	_render_ns_total.store(0, std::memory_order_relaxed);
	_render_frames_total.store(0, std::memory_order_relaxed);
	_render_ns_worst.store(0, std::memory_order_relaxed);
	_worst_block_frames.store(0, std::memory_order_relaxed);
	_state.reset_counters();
}

Ref<AudioStreamPlayback> NoiseVoiceStream::_instantiate_playback() const {
	Ref<NoiseVoicePlayback> playback;
	playback.instantiate();

	// The sample rate is read here rather than in `_mix`. `AudioServer::get_mix_rate`
	// is a call into the engine and the audio thread must make none, so the rate is
	// captured once on the main thread and both synths are sized to it.
	AudioServer *server = AudioServer::get_singleton();
	const double rate = server != nullptr ? server->get_mix_rate() : 48000.0;

	// `const_cast` to hand the playback a counted reference to its own stream. The
	// stream does not reference the playback, so there is no cycle; what this buys is
	// that a playback outliving its stream reads a live object instead of a freed one.
	playback->bind(Ref<NoiseVoiceStream>(const_cast<NoiseVoiceStream *>(this)), rate);
	_mix_rate.store(rate, std::memory_order_relaxed);
	return playback;
}

String NoiseVoiceStream::_get_stream_name() const {
	return get_layer() == LAYER_WIND ? String("Wind noise") : String("Tire scrub");
}

double NoiseVoiceStream::_get_length() const {
	// Zero means unbounded to the player, which is what a live synth is.
	return 0.0;
}

bool NoiseVoiceStream::_is_monophonic() const {
	return true;
}

// --- NoiseVoicePlayback -----------------------------------------------------

void NoiseVoicePlayback::_bind_methods() {}

void NoiseVoicePlayback::bind(const Ref<NoiseVoiceStream> &p_stream, double p_sample_rate) {
	_stream = p_stream;
	_rate = p_sample_rate > 1.0 ? p_sample_rate : 48000.0;

	// Both synths get the same config and the same rate. Which one renders is the
	// stream's `layer`, read per block, so a scene that flips the layer after
	// `play()` gets the other layer rather than silence.
	kart::core::ScrubWindConfig config;
	if (_stream.is_valid()) {
		const double gain = _stream->get_gain();
		config.scrub_gain = gain;
		config.wind_gain = gain;
	}
	_scrub.configure(config, _rate);
	_wind.configure(config, _rate);
	_last_good = kart::core::EngineAudioInput();
}

int32_t NoiseVoicePlayback::_mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) {
	// `p_rate_scale` is the pitch scale the player asks for, and it is ignored for
	// the same reason the engine voice ignores it: a synthesized layer computes its
	// own spectrum from vehicle state and has no source rate to resample from.
	(void)p_rate_scale;

	if (p_buffer == nullptr || p_frames <= 0) {
		return 0;
	}
	if (_stream.is_null()) {
		for (int32_t i = 0; i < p_frames; ++i) {
			p_buffer[i].left = 0.0f;
			p_buffer[i].right = 0.0f;
		}
		return p_frames;
	}

	// The live knobs, re-read every block. `engine_voice.cpp` records the defect
	// that capturing a gain at bind time caused, and this file is not going to
	// reintroduce it: two relaxed atomic loads, far below anything the worst-block
	// figure can see.
	const bool is_wind = _stream->get_layer() == NoiseVoiceStream::LAYER_WIND;
	const kart::core::ScrubWindConfig config = _stream->tuning_config();
	if (is_wind) {
		_wind.set_tuning(config);
	} else {
		_scrub.set_tuning(config);
	}

	// One seqlock read per block, which is the rate ADR-0035 costed at 4.6 ns.
	// Reading per frame would be 512 times that for a state that changes every 1.39
	// blocks.
	kart::core::EngineAudioInput input;
	if (_stream->read(input)) {
		_last_good = input;
	} else {
		// Budget exhausted. Repeat the last good tick rather than render a torn one.
		input = _last_good;
	}
	if (is_wind) {
		_wind.publish(input);
	} else {
		_scrub.publish(input);
	}

	const int64_t start_ns = now_ns();
	int32_t done = 0;
	while (done < p_frames) {
		int32_t chunk = p_frames - done;
		if (chunk > SCRATCH_FRAMES) {
			chunk = SCRATCH_FRAMES;
		}
		if (is_wind) {
			_wind.render(_scratch, chunk);
		} else {
			_scrub.render(_scratch, chunk);
		}
		// Mono to both channels. The player above this owns panning and distance; a
		// synth that panned itself would fight it. For the wind layer that player is a
		// plain `AudioStreamPlayer` and there is nothing to fight, which is the point:
		// wind is not a source in the world.
		for (int32_t i = 0; i < chunk; ++i) {
			const float sample = _scratch[i];
			p_buffer[done + i].left = sample;
			p_buffer[done + i].right = sample;
		}
		done += chunk;
	}
	const int64_t render_ns = now_ns() - start_ns;

	NoiseVoiceStream *stream = _stream.ptr();
	stream->_mix_calls.fetch_add(1, std::memory_order_relaxed);
	stream->_render_ns_total.fetch_add(render_ns, std::memory_order_relaxed);
	stream->_render_frames_total.fetch_add(p_frames, std::memory_order_relaxed);
	stream->_level.store(is_wind ? _wind.level() : _scrub.level(), std::memory_order_relaxed);

	// The worst block, kept with a compare-exchange so that two playbacks reporting
	// into one stream cannot lose the larger of them. The block size is stored with
	// it because a worst *block* is only comparable per frame, and the largest block
	// is not necessarily the slowest one.
	int64_t previous_worst = stream->_render_ns_worst.load(std::memory_order_relaxed);
	while (render_ns > previous_worst) {
		if (stream->_render_ns_worst.compare_exchange_weak(previous_worst, render_ns,
					std::memory_order_relaxed, std::memory_order_relaxed)) {
			stream->_worst_block_frames.store(p_frames, std::memory_order_relaxed);
			break;
		}
	}

	_position += static_cast<double>(p_frames) / _rate;
	return p_frames;
}

void NoiseVoicePlayback::_start(double p_from_pos) {
	_active = true;
	_position = p_from_pos;
	// Not a `reset()`. Both synths are already clean from `bind`, and `reset` reseeds
	// the PRNG and clears the filter — which on this thread, whose identity this
	// project has not measured, is work that does not need doing here.
}

void NoiseVoicePlayback::_stop() {
	_active = false;
}

bool NoiseVoicePlayback::_is_playing() const {
	return _active;
}

int32_t NoiseVoicePlayback::_get_loop_count() const {
	return 0;
}

double NoiseVoicePlayback::_get_playback_position() const {
	return _position;
}

void NoiseVoicePlayback::_seek(double p_position) {
	// A live synth has nothing to seek to. Accepting the position and ignoring it is
	// honest; refusing it would make `AudioStreamPlayer` log a failure every time
	// something in the scene tried.
	_position = p_position;
}

} // namespace kartgame
