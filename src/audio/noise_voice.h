#ifndef KARTGAME_AUDIO_NOISE_VOICE_H
#define KARTGAME_AUDIO_NOISE_VOICE_H

#include "audio/state_seqlock.h"
#include "core/audio_state.h"
#include "core/scrub_wind.h"

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <atomic>
#include <cstdint>

namespace kartgame {

// Tire scrub and wind joined to Godot's mixer. Issue #84, ARCHITECTURE.md §12.
//
// `src/core/scrub_wind.h` is the DSP and knows nothing about Godot. This is the
// production join and it is as thin as `engine_voice.{h,cpp}` is: it decides
// nothing about how either layer sounds, only who calls `render` and how the
// solver's state reaches it.
//
// ## One class, two layers, and why that is not the same as one stream
//
// `scrub_wind.h`'s header says the two layers must be separable at the mixer:
// scrub is positional because M8's acceptance criterion is that an opponent is
// locatable by ear, wind is at the driver's head and would be nonsense to
// attenuate or Doppler-shift, and issue #160 needs turning one off not to require
// re-judging the other. All of that is about **players**, not about classes.
//
// So there is one stream class with a `layer` property and a scene instantiates
// two of them: one on an `AudioStreamPlayer3D` at the kart, one on a plain
// `AudioStreamPlayer`. The alternative — two nearly identical class pairs — would
// be twice the registration, twice the seqlock plumbing and two places to fix a
// bug, to express a difference that is entirely in how the scene mounts them.
//
// The playback holds both synths and renders the selected one. Each is about a
// hundred bytes of filter state and two PRNG words, so carrying the unused one
// costs less than a branch on a pointer would.
//
// ## Cost, measured before any of this was written
//
// `tools/verify/scrub_cost_probe.gd`, inside a real CoreAudio `_mix`:
//
//     one engine voice, worst (quoted)     1412.8 ns/frame
//     one scrub layer, worst                  12.6 ns/frame   0.89 % of a voice
//     the one wind layer, by difference        9.0 ns/frame
//     twelve scrub layers (M7)                 0.667 % of real time
//
// Flat across every operating point, and the `silent` cell is among the most
// expensive of the five — which is the check that nothing short-circuits when the
// drive is zero. A layer that were cheap exactly when nobody is listening would be
// the wrong shape.
//
// This matters because it is what the figure in `engine_voice.h` lands on top of:
// twelve engine voices are **74.76% of real time** and that is the uncomfortable
// number M7 has to solve. Twelve scrub layers add two thirds of one percent to it.
// Whatever M7 does about voice culling, it is not about this file.
class NoiseVoiceStream : public godot::AudioStream {
	GDCLASS(NoiseVoiceStream, godot::AudioStream)

protected:
	static void _bind_methods();

public:
	// Which layer this stream renders. Named rather than a bool because a third
	// noise layer is already on the roadmap — §12's surface-dependent rolling, issue
	// #85 — and `is_wind = false` would be a poor way to say "rolling".
	enum Layer : int {
		LAYER_SCRUB = 0,
		LAYER_WIND = 1,
	};

	virtual godot::Ref<godot::AudioStreamPlayback> _instantiate_playback() const override;
	virtual godot::String _get_stream_name() const override;
	virtual double _get_length() const override;
	virtual bool _is_monophonic() const override;

	// Publish one tick of solver state. Physics thread, at the tick rate, and the
	// only way state reaches either synth.
	//
	// **Both layers read the same struct**, and that is deliberate rather than
	// convenient: `EngineAudioInput` already carries `scrub`, `speed_ms` and
	// `surface`, they are computed once in `KartBody`, and a second payload type
	// would mean a second place for the aggregation rule to be restated. A kart
	// therefore calls `publish` on each of its noise streams with the same struct it
	// hands the engine voice.
	void publish(const kart::core::EngineAudioInput &p_input);
	bool read(kart::core::EngineAudioInput &r_input) const;

	void set_layer(int p_layer);
	int get_layer() const;

	// The layer's output gain, linear. The one knob a scene sets, and the one issue
	// #160's mixing pass will own. Everything else about the sound is in
	// `scrub_wind_tuning`, where it is a declared guess with a tuning-registry row
	// rather than something a scene may quietly override.
	//
	// Re-read every mix block rather than captured at bind time. `engine_voice.cpp`
	// records why: capturing it once meant `set_gain` after `play()` did nothing at
	// all, and nothing noticed because the one caller happened to set it first.
	void set_gain(double p_gain);
	double get_gain() const;

	// The six spectral rows `core/tuning.h` carries for this layer, exposed under
	// their tunable keys so `engine_voice_rig.gd` can forward a `tuning_changed`
	// straight through without a translation table.
	//
	// **All six live on both layers even though each belongs to one**, and that is
	// cheaper than it looks: they are `std::atomic<double>` on a stream, the
	// playback assembles a `ScrubWindConfig` per block, and the layer it is not
	// rendering ignores the half that is not its own. The alternative — two stream
	// classes with disjoint property sets — is the two-class design this file's
	// header already rejected for a better reason.
	//
	// Re-read every mix block. `ScrubSynth::set_tuning` and `WindSynth::set_tuning`
	// are documented as audio-thread safe where `configure` is not: they copy
	// scalars and touch no filter state, so a knob moving mid-corner is a
	// coefficient step and not a click.
	void set_scrub_center_hz(double p_hz);
	double get_scrub_center_hz() const;
	void set_scrub_q(double p_q);
	double get_scrub_q() const;
	void set_scrub_gamma(double p_gamma);
	double get_scrub_gamma() const;
	void set_scrub_full_speed(double p_ms);
	double get_scrub_full_speed() const;
	void set_wind_cutoff_per_ms(double p_hz_per_ms);
	double get_wind_cutoff_per_ms() const;
	void set_wind_speed_exponent(double p_exponent);
	double get_wind_speed_exponent() const;

	// The whole config as the synths want it, assembled from the atomics above.
	// Audio-thread call, on the block boundary.
	kart::core::ScrubWindConfig tuning_config() const;

	// What the audio thread is doing, for the telemetry panel. Same shape and same
	// caveat as `EngineVoiceStream::voice_stats`: a snapshot of relaxed counters and
	// not a transaction, which is the right trade for a diagnostic.
	godot::Dictionary voice_stats() const;
	void reset_stats();

private:
	friend class NoiseVoicePlayback;

	// The transport. `audio/state_seqlock.h` owns the algorithm and the argument for
	// why it is not `std::atomic<EngineAudioInput>`.
	AudioStateSeqlock _state;

	// Atomic rather than plain because the main thread writes them while `_mix` may
	// be running: ADR-0035 answer 4 measured 213 of 960 physics ticks overlapping a
	// mix, so "nobody is mixing right now" is never true.
	std::atomic<int32_t> _layer{ LAYER_SCRUB };

	// Seeded from `ScrubWindConfig`'s own defaults so a stream nobody tunes sounds
	// exactly as the header's numbers say it should. A zero default here would
	// silently delete the layer from every scene that forgot to set it.
	std::atomic<double> _gain{ kart::core::ScrubWindConfig().scrub_gain };

	// Seeded from `ScrubWindConfig`'s own defaults, which are in turn derived from
	// `kz_audio_reference.h`'s measured figures. A stream nobody tunes therefore
	// sounds exactly as `scrub_wind.h`'s header says it should, and there is no
	// second copy of any number here.
	std::atomic<double> _scrub_center_hz{ kart::core::ScrubWindConfig().scrub_center_hz };
	std::atomic<double> _scrub_q{ kart::core::ScrubWindConfig().scrub_q };
	std::atomic<double> _scrub_gamma{ kart::core::ScrubWindConfig().scrub_gamma };
	std::atomic<double> _scrub_full_speed{ kart::core::ScrubWindConfig().scrub_full_speed_ms };
	std::atomic<double> _wind_cutoff_per_ms{ kart::core::ScrubWindConfig().wind_cutoff_hz_per_ms };
	std::atomic<double> _wind_speed_exponent{ kart::core::ScrubWindConfig().wind_speed_exponent };

	mutable std::atomic<int64_t> _mix_calls{ 0 };
	mutable std::atomic<int64_t> _render_ns_total{ 0 };
	mutable std::atomic<int64_t> _render_frames_total{ 0 };
	mutable std::atomic<int64_t> _render_ns_worst{ 0 };
	mutable std::atomic<int32_t> _worst_block_frames{ 0 };
	mutable std::atomic<double> _mix_rate{ 0.0 };
	mutable std::atomic<double> _level{ 0.0 };
};

// The playback half. Owns one of each synth and does nothing else.
//
// One pair per playback rather than per stream, for the reason `engine_voice.h`
// gives: Godot may instantiate several playbacks from one stream, and two
// playbacks sharing a synth would share its filter state and its PRNG, which is
// not two voices but one voice rendered twice into the same state.
class NoiseVoicePlayback : public godot::AudioStreamPlayback {
	GDCLASS(NoiseVoicePlayback, godot::AudioStreamPlayback)

protected:
	static void _bind_methods();

public:
	virtual int32_t _mix(godot::AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) override;
	virtual void _start(double p_from_pos) override;
	virtual void _stop() override;
	virtual bool _is_playing() const override;
	virtual int32_t _get_loop_count() const override;
	virtual double _get_playback_position() const override;
	virtual void _seek(double p_position) override;

	// Bind the playback to its stream and size both synths. Main thread only:
	// `configure` resets a filter and reseeds a PRNG, and is documented as not an
	// audio-thread call.
	//
	// Called from `_instantiate_playback`, which is reached out of
	// `AudioStreamPlayer::play()` on the main thread. Not from `_start`, whose thread
	// this project has never measured — and "`_start` is surely the main thread" is
	// exactly the shape of the six pieces of assumed engine behavior ADR-0033 and
	// ADR-0035 found to be wrong.
	void bind(const godot::Ref<NoiseVoiceStream> &p_stream, double p_sample_rate);

private:
	// Mono scratch, sized past the 512 frames ADR-0035 measured on every one of 684
	// calls, so a device with a larger block degrades into two renders rather than a
	// buffer overrun.
	static constexpr int SCRATCH_FRAMES = 4096;

	godot::Ref<NoiseVoiceStream> _stream;
	kart::core::ScrubSynth _scrub;
	kart::core::WindSynth _wind;
	float _scratch[SCRATCH_FRAMES] = {};

	// The last snapshot that read cleanly, held so an exhausted retry budget repeats
	// a tick of state instead of using a torn one. At 120 Hz that is 8.3 ms of an
	// unchanged level, which is inaudible on a noise layer.
	kart::core::EngineAudioInput _last_good{};

	bool _active = false;
	double _position = 0.0;
	double _rate = 48000.0;
};

} // namespace kartgame

VARIANT_ENUM_CAST(kartgame::NoiseVoiceStream::Layer);

#endif // KARTGAME_AUDIO_NOISE_VOICE_H
