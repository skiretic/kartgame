#ifndef KARTGAME_AUDIO_ENGINE_VOICE_H
#define KARTGAME_AUDIO_ENGINE_VOICE_H

#include "audio/state_seqlock.h"
#include "core/audio_state.h"
#include "core/engine_synth.h"

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <atomic>
#include <cstdint>

namespace kartgame {

// The kart's engine note: `EngineSynth` joined to Godot's mixer. Issues #81, #82.
//
// `src/core/engine_synth.h` is the synthesizer and knows nothing about Godot.
// `src/audio/audio_probe.{h,cpp}` measured the boundary between them. This is the
// production join, and it is deliberately thin — it decides nothing about how the
// engine sounds, only who calls `render` and how the solver's state reaches it.
//
// ## Architecture (b), because it was measured
//
// ADR-0035 chose a custom `AudioStream` / `AudioStreamPlayback` pair overriding
// `_mix` over `AudioStreamGenerator` and its ring, on one number: **11.6 ms of
// latency against 172.7 ms** at Godot's own default `buffer_length`. §12 calls the
// engine note a primary feel channel and a sixth of a second of lag is not one.
// The generator also underran 4-8 times at startup with no load at all, before any
// deliberate stall, because its ring is empty at `play()`.
//
// Issue #81's title still names the losing architecture. ADR-0035 records why.
//
// ## The transport
//
// A seqlock, and it now lives in `audio/state_seqlock.h` rather than here: issue
// #84 added two more streams that need the identical transport, and three
// hand-copied seqlocks is three places for the memory ordering to drift apart.
// That header carries the full argument for why it is not
// `std::atomic<EngineAudioInput>` and the measurements behind it. Nothing about
// the algorithm changed in the move.
//
// ## Where the seqlock lives, and why not at namespace scope
//
// In the stream, which is a `Resource` the scene holds. Not in a namespace-scope
// singleton like the probe's `g_stats`, even though that would be less typing:
// CLAUDE.md's `dyld4::callInitializer` trap is that a namespace-scope object in
// this extension runs its constructor inside `dlopen`, before godot-cpp has bound
// its interface. The probe survives it only because every member of `ProbeStats` is
// a `constexpr`-constructible `std::atomic` and is therefore constant-initialized.
// A stream that is instantiated like any other object has no such constraint to
// remember, and one fewer global is one fewer way to reintroduce the crash.
//
// It also means a second kart is a second stream rather than a rewrite, which M7
// will want — see the cost note below.
//
// ## Cost, measured, and the one number that is not comfortable
//
// `tools/verify/synth_cost_probe.gd` runs `EngineSynth::render` inside a real
// CoreAudio `_mix`. The stack is filled to a frequency ceiling rather than a fixed
// index, so the partial count *falls* with rpm and **idle is the worst case**: 191
// partials at 2,000-2,500 rpm against 33 at the soft cut.
//
//     rpm     partials   ns/frame   % of real time   worst block
//     2500       191       1412.8        6.23 %        11.06 %
//     5000        96        774.3        3.41 %         6.04 %
//    14300        33        314.4        1.39 %         2.52 %
//
// Cost is linear in partial count — 6.815 ns per partial per frame plus 89.5 ns of
// fixed cost, worst residual 3.9% of the swept range — so one voice is affordable
// with room to spare and needs no cap.
//
// **Twelve voices is 74.76% of real time**, and M7 wants twelve karts. That is a
// real constraint on a milestone that has not started, recorded here because this
// is the file that would have to change: voice culling by distance, or one shared
// stack for the pack. Not solved, and not this milestone's problem.
class EngineVoiceStream : public godot::AudioStream {
	GDCLASS(EngineVoiceStream, godot::AudioStream)

protected:
	static void _bind_methods();

public:
	virtual godot::Ref<godot::AudioStreamPlayback> _instantiate_playback() const override;
	virtual godot::String _get_stream_name() const override;
	virtual double _get_length() const override;
	virtual bool _is_monophonic() const override;

	// Publish one tick of solver state. Called from the physics thread at the tick
	// rate, and it is the only way state reaches the synth.
	//
	// Wait-free for the writer: two relaxed increments of a counter with a release
	// fence between them and the payload. It cannot block, so a slow audio thread
	// can never slow the solver — which is the property that matters, because the
	// alternative is a physics tick that stalls on a mixer.
	void publish(const kart::core::EngineAudioInput &p_input);

	// Read the published state. The audio-thread call. Returns false when the retry
	// budget was exhausted, leaving `r_input` untouched — the caller substitutes its
	// own last good snapshot rather than rendering a torn one, and
	// `EngineVoicePlayback::_last_good` is where that snapshot lives. A repeated
	// frame of engine state is inaudible; a torn one is a partial's frequency
	// stepping to a value no engine state ever had.
	bool read(kart::core::EngineAudioInput &r_input) const;

	// Overall output gain, linear. The one knob exposed to the scene, because it is
	// the one a driver turns. Everything else about the sound is a measured constant
	// in `kz_audio_reference.h` or a documented tunable in `synth_tuning`, and a
	// scene that could override those would be a second place for the numbers to
	// live. Issue #83's mixing controls widen this deliberately, later.
	void set_gain(double p_gain);
	double get_gain() const;

	// The two synth constants issue #159 lists as unsourced and that only an ear
	// can settle: the exhaust comb depth and the broadband noise layer.
	//
	// **These widen the paragraph above rather than contradicting it**, and the
	// distinction is the one `src/core/tuning.h` exists to hold. `comb_depth` is
	// derived — 0.30 is the smallest default landing inside a measured 1.6-2.6 dB
	// ripple band — and `noise_gain` has no measurement at all. Neither is a
	// figure in `kz_audio_reference.h` that a scene would be overriding; both are
	// guesses this project made, and the tuning registry is the disclosed place
	// to move a guess. A scene that reached for `PIPE_LADDER_DB` would still be
	// doing the thing the paragraph above forbids.
	//
	// Applied at the top of a mix block rather than through `configure()`, which
	// clears the comb line and reseeds the noise RNG. See
	// `EngineSynth::set_comb_depth`.
	void set_comb_depth(double p_depth);
	double get_comb_depth() const;
	void set_noise_gain(double p_gain);
	double get_noise_gain() const;

	// What the audio thread is doing, for the HUD and for the telemetry panel.
	//
	// Every field is a counter the audio thread writes with relaxed ordering and
	// the main thread reads, so this is a snapshot and not a transaction. That is
	// the right trade for a diagnostic: a consistent read would need the audio
	// thread to publish through a second seqlock, which is real cost on the
	// deadline in exchange for a HUD row being self-consistent.
	//
	// `render_ns_worst` is the number that matters and the reason this exists at
	// all. The median is comfortable at every operating point; a dropout is caused
	// by one block, and a worst-block figure is the only thing that sees it.
	godot::Dictionary voice_stats() const;

	// Clear the counters `voice_stats` reports. Not the seqlock: resetting a
	// transport mid-flight is how a reader gets a payload from before the reset with
	// a sequence number from after it.
	void reset_stats();

private:
	friend class EngineVoicePlayback;

	// The transport. `audio/state_seqlock.h` owns the algorithm and the argument.
	AudioStateSeqlock _state;

	// Configuration the audio thread reads. Atomic rather than plain because the
	// main thread writes them while `_mix` may be running: ADR-0035 answer 4
	// measured 213 of 960 physics ticks overlapping a mix, so "nobody is mixing
	// right now" is never true.
	std::atomic<double> _gain{ 0.0 };

	// Seeded from `EngineAudioConfig`'s own defaults so that a stream nobody
	// tunes sounds exactly as it did before these existed. A zero default here
	// would have silently deleted the noise layer from every scene.
	std::atomic<double> _comb_depth{ kart::core::EngineAudioConfig().comb_depth };
	std::atomic<double> _noise_gain{ kart::core::EngineAudioConfig().noise_gain };

	// --- what the audio thread reports back ---------------------------------
	mutable std::atomic<int64_t> _mix_calls{ 0 };
	mutable std::atomic<int64_t> _render_ns_total{ 0 };
	mutable std::atomic<int64_t> _render_frames_total{ 0 };
	mutable std::atomic<int64_t> _render_ns_worst{ 0 };
	mutable std::atomic<int32_t> _worst_block_frames{ 0 };
	mutable std::atomic<int32_t> _partials{ 0 };
	mutable std::atomic<double> _mix_rate{ 0.0 };

	// The rpm the synth was actually rendering at the end of the last block.
	//
	// **Not the same fact as `_partials`, and that used to be missable.** The stack
	// size was the only rpm-dependent number a probe could see, so
	// `tools/verify/voice_probe.gd` used a *changing partial count* as its evidence
	// that rpm had crossed the thread boundary at all. That inference held only
	// because the stack ended at a fixed frequency and so shrank as f0 rose — 191
	// partials at idle down to 33 at the cut. The stack now ends at a fixed
	// harmonic count and its size barely moves, which does not break the audio and
	// does break the probe's only witness. Publishing the rpm makes the check
	// direct instead of circumstantial, and a direct check is what should have been
	// there when the indirect one still worked.
	mutable std::atomic<double> _render_rpm{ 0.0 };
};

// The playback half. Owns one `EngineSynth` and does nothing else.
//
// One synth per playback rather than one per stream, because Godot may instantiate
// several playbacks from one stream — polyphony it does not need but does not
// forbid — and two playbacks sharing a synth would share its phase accumulators
// and its comb line, which is not two voices but one voice rendered twice into the
// same state.
class EngineVoicePlayback : public godot::AudioStreamPlayback {
	GDCLASS(EngineVoicePlayback, godot::AudioStreamPlayback)

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

	// Bind the playback to its stream and size the synth. Main thread only —
	// `EngineSynth::configure` fills a 4,096-point sine table and both ladder
	// tables, and its own comment says it is not an audio-thread call.
	//
	// Called from `_instantiate_playback`, which is reached out of
	// `AudioStreamPlayer::play()` on the main thread. Not from `_start`, whose
	// thread this project has never measured: ADR-0033 and ADR-0035 between them
	// found six pieces of assumed engine behavior to be wrong, and "`_start` is
	// surely called on the main thread" is exactly the shape of all six.
	void bind(const godot::Ref<EngineVoiceStream> &p_stream, double p_sample_rate);

private:
	// Mono scratch the synth renders into before it is fanned out to `AudioFrame`.
	// Sized past the 512 frames ADR-0035 measured on every one of 684 calls, so a
	// device with a larger block degrades into two renders rather than a buffer
	// overrun.
	static constexpr int SCRATCH_FRAMES = 4096;

	godot::Ref<EngineVoiceStream> _stream;
	kart::core::EngineSynth _synth;
	float _scratch[SCRATCH_FRAMES] = {};

	// The last snapshot that read cleanly. Held so that an exhausted retry budget
	// repeats a frame of engine state instead of using a torn one. At 120 Hz a
	// repeated tick is 8.3 ms of an unchanged note.
	kart::core::EngineAudioInput _last_good{};

	bool _active = false;
	double _position = 0.0;
	double _rate = 48000.0;
};

} // namespace kartgame

#endif // KARTGAME_AUDIO_ENGINE_VOICE_H
