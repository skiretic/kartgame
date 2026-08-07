#include "audio/engine_voice.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <chrono>
#include <cstring>

using namespace godot;

namespace kartgame {

namespace {

int64_t now_ns() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count();
}

} // namespace

// --- EngineVoiceStream ------------------------------------------------------

void EngineVoiceStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_gain", "gain"), &EngineVoiceStream::set_gain);
	ClassDB::bind_method(D_METHOD("get_gain"), &EngineVoiceStream::get_gain);
	ClassDB::bind_method(D_METHOD("set_comb_depth", "depth"), &EngineVoiceStream::set_comb_depth);
	ClassDB::bind_method(D_METHOD("get_comb_depth"), &EngineVoiceStream::get_comb_depth);
	ClassDB::bind_method(D_METHOD("set_noise_gain", "gain"), &EngineVoiceStream::set_noise_gain);
	ClassDB::bind_method(D_METHOD("get_noise_gain"), &EngineVoiceStream::get_noise_gain);
	ClassDB::bind_method(D_METHOD("voice_stats"), &EngineVoiceStream::voice_stats);
	ClassDB::bind_method(D_METHOD("reset_stats"), &EngineVoiceStream::reset_stats);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"),
			"set_gain", "get_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "comb_depth", PROPERTY_HINT_RANGE, "0.0,0.8,0.01"),
			"set_comb_depth", "get_comb_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_gain", PROPERTY_HINT_RANGE, "0.0,0.6,0.01"),
			"set_noise_gain", "get_noise_gain");
}

void EngineVoiceStream::publish(const kart::core::EngineAudioInput &p_input) {
	_state.publish(p_input);
}

bool EngineVoiceStream::read(kart::core::EngineAudioInput &r_input) const {
	return _state.read(r_input);
}

void EngineVoiceStream::set_gain(double p_gain) {
	_gain.store(p_gain < 0.0 ? 0.0 : (p_gain > 1.0 ? 1.0 : p_gain), std::memory_order_relaxed);
}

double EngineVoiceStream::get_gain() const {
	return _gain.load(std::memory_order_relaxed);
}

void EngineVoiceStream::set_comb_depth(double p_depth) {
	_comb_depth.store(p_depth < 0.0 ? 0.0 : (p_depth > 1.0 ? 1.0 : p_depth),
			std::memory_order_relaxed);
}

double EngineVoiceStream::get_comb_depth() const {
	return _comb_depth.load(std::memory_order_relaxed);
}

void EngineVoiceStream::set_noise_gain(double p_gain) {
	_noise_gain.store(p_gain < 0.0 ? 0.0 : p_gain, std::memory_order_relaxed);
}

double EngineVoiceStream::get_noise_gain() const {
	return _noise_gain.load(std::memory_order_relaxed);
}

Dictionary EngineVoiceStream::voice_stats() const {
	Dictionary d;
	const int64_t calls = _mix_calls.load(std::memory_order_relaxed);
	const int64_t frames = _render_frames_total.load(std::memory_order_relaxed);
	const int64_t total_ns = _render_ns_total.load(std::memory_order_relaxed);
	const int64_t worst_ns = _render_ns_worst.load(std::memory_order_relaxed);
	const int32_t worst_frames = _worst_block_frames.load(std::memory_order_relaxed);
	const double rate = _mix_rate.load(std::memory_order_relaxed);

	d["mix_calls"] = calls;
	d["partials"] = _partials.load(std::memory_order_relaxed);
	d["mix_rate"] = rate;
	// The rpm and the fundamental the audio thread last rendered. `voice_probe.gd`
	// reads these with `[]` and not `get(key, default)` — a renamed key has to fail
	// loudly rather than draw a zero forever, which is the trap `kart_body.cpp`
	// warns about over its own telemetry Dictionary.
	d["render_rpm"] = _render_rpm.load(std::memory_order_relaxed);
	d["render_f0_hz"] = kart::core::kz_audio::rpm_to_f0_hz(
			_render_rpm.load(std::memory_order_relaxed));
	d["seq_gave_up"] = _state.gave_up();
	d["seq_retries"] = _state.retries();

	// Per frame, because the block size is the device's choice and the frame period
	// is the deadline's unit.
	d["render_ns_per_frame"] = frames > 0 ? static_cast<double>(total_ns) / static_cast<double>(frames) : 0.0;
	d["render_ns_worst_per_frame"] = worst_frames > 0
			? static_cast<double>(worst_ns) / static_cast<double>(worst_frames)
			: 0.0;

	// The fraction of real time the synth consumed, mean and worst block. This is
	// the figure that either does or does not starve the device, and §15's 0.5 ms of
	// a rendered frame is *not* — the audio thread spends none of a rendered frame.
	// Issue #155 restates that row; `tools/verify/synth_cost_probe.gd` holds the
	// measurement it gets restated against.
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

void EngineVoiceStream::reset_stats() {
	_mix_calls.store(0, std::memory_order_relaxed);
	_render_ns_total.store(0, std::memory_order_relaxed);
	_render_frames_total.store(0, std::memory_order_relaxed);
	_render_ns_worst.store(0, std::memory_order_relaxed);
	_worst_block_frames.store(0, std::memory_order_relaxed);
	_state.reset_counters();
}

Ref<AudioStreamPlayback> EngineVoiceStream::_instantiate_playback() const {
	Ref<EngineVoicePlayback> playback;
	playback.instantiate();

	// The sample rate is read here rather than in `_mix`. `AudioServer::get_mix_rate`
	// is a call into the engine and the audio thread must make none, so the rate is
	// captured once on the main thread and the synth is sized to it.
	AudioServer *server = AudioServer::get_singleton();
	const double rate = server != nullptr ? server->get_mix_rate() : 48000.0;

	// `const_cast` to hand the playback a counted reference to its own stream. The
	// stream does not reference the playback, so there is no cycle; what this buys
	// is that a playback outliving its stream reads a live object instead of a freed
	// one. `_instantiate_playback` is const because it does not mutate the stream's
	// *stream-ness*, which is a different question from refcounting it.
	playback->bind(Ref<EngineVoiceStream>(const_cast<EngineVoiceStream *>(this)), rate);
	_mix_rate.store(rate, std::memory_order_relaxed);
	return playback;
}

String EngineVoiceStream::_get_stream_name() const {
	return String("KZ engine voice");
}

double EngineVoiceStream::_get_length() const {
	// Zero means unbounded to the player, which is what a live synth is.
	return 0.0;
}

bool EngineVoiceStream::_is_monophonic() const {
	return true;
}

// --- EngineVoicePlayback ----------------------------------------------------

void EngineVoicePlayback::_bind_methods() {}

void EngineVoicePlayback::bind(const Ref<EngineVoiceStream> &p_stream, double p_sample_rate) {
	_stream = p_stream;
	_rate = p_sample_rate > 1.0 ? p_sample_rate : 48000.0;

	kart::core::EngineAudioConfig config;
	if (_stream.is_valid()) {
		config.gain = _stream->get_gain();
		config.comb_depth = _stream->get_comb_depth();
		config.noise_gain = _stream->get_noise_gain();
	}
	_synth.configure(config, _rate);
	_last_good = kart::core::EngineAudioInput();
}

int32_t EngineVoicePlayback::_mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) {
	// `p_rate_scale` is the pitch scale the player asks for, and it is ignored on
	// purpose: a synthesized note computes its own frequency from rpm and has no
	// source rate to resample from, so honoring it would pitch-shift the engine
	// away from the rpm the driver is reading off the tachometer.
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

	// The three live knobs, re-read every block.
	//
	// **This fixes a defect the tuning registry exposed rather than caused.**
	// `bind()` above read `get_gain()` once and the synth kept that value
	// forever, so `set_gain` after the voice started playing did nothing at all.
	// Nothing noticed because `scripts/game/engine_voice_rig.gd` happens to set
	// the gain before `play()`, so the one call that mattered landed on the right
	// side of the ordering. A tuning overlay turning the volume knob mid-corner
	// would have found it immediately, and the symptom would have been read as
	// "the overlay does not work" rather than as this.
	//
	// Three relaxed atomic loads per block. ADR-0035 costed the seqlock read at
	// 4.6 ns and 0.0013% of the budget; these are cheaper and there are three of
	// them, so it stays far below anything the worst-block figure can see.
	if (_stream.is_valid()) {
		_synth.set_gain(_stream->get_gain());
		_synth.set_comb_depth(_stream->get_comb_depth());
		_synth.set_noise_gain(_stream->get_noise_gain());
	}

	// One seqlock read per block, which is the rate ADR-0035 costed at 4.6 ns and
	// 0.0013% of the budget. Reading per frame would be 512 times that for a state
	// that only changes every 1.39 blocks.
	kart::core::EngineAudioInput input;
	if (_stream->read(input)) {
		_last_good = input;
	} else {
		// Budget exhausted. Repeat the last good tick rather than render a torn one:
		// a partial whose harmonic number came from one publish and whose rpm came
		// from another is a frequency that no engine state ever had.
		input = _last_good;
	}
	_synth.publish(input);

	const int64_t start_ns = now_ns();
	int32_t done = 0;
	while (done < p_frames) {
		int32_t chunk = p_frames - done;
		if (chunk > SCRATCH_FRAMES) {
			chunk = SCRATCH_FRAMES;
		}
		_synth.render(_scratch, chunk);
		// Mono to both channels. The player above this — an `AudioStreamPlayer3D` on
		// the engine mount — owns panning and distance; a synth that panned itself
		// would fight it.
		for (int32_t i = 0; i < chunk; ++i) {
			const float sample = _scratch[i];
			p_buffer[done + i].left = sample;
			p_buffer[done + i].right = sample;
		}
		done += chunk;
	}
	const int64_t render_ns = now_ns() - start_ns;

	EngineVoiceStream *stream = _stream.ptr();
	stream->_mix_calls.fetch_add(1, std::memory_order_relaxed);
	stream->_render_ns_total.fetch_add(render_ns, std::memory_order_relaxed);
	stream->_render_frames_total.fetch_add(p_frames, std::memory_order_relaxed);
	stream->_partials.store(static_cast<int32_t>(_synth.partial_count()), std::memory_order_relaxed);
	stream->_render_rpm.store(_synth.current_rpm(), std::memory_order_relaxed);

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

void EngineVoicePlayback::_start(double p_from_pos) {
	_active = true;
	_position = p_from_pos;
	// Not `_synth.reset()`. `reset` clears the sine table's consumers, the comb line
	// and every phase — cheap, but this may be the audio thread and the phases are
	// exactly what must survive a restart to avoid a click. The synth is already
	// clean from `bind`.
}

void EngineVoicePlayback::_stop() {
	_active = false;
}

bool EngineVoicePlayback::_is_playing() const {
	return _active;
}

int32_t EngineVoicePlayback::_get_loop_count() const {
	return 0;
}

double EngineVoicePlayback::_get_playback_position() const {
	return _position;
}

void EngineVoicePlayback::_seek(double p_position) {
	// A live synth has nothing to seek to. Accepting the position and ignoring it is
	// honest; refusing it would make `AudioStreamPlayer` log a failure every time
	// something in the scene tried.
	_position = p_position;
}

} // namespace kartgame
