#ifndef KARTGAME_AUDIO_AUDIO_PROBE_H
#define KARTGAME_AUDIO_AUDIO_PROBE_H

#include "core/audio_state.h"
#include "core/engine_synth.h"
#include "core/scrub_wind.h"

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <atomic>
#include <cstdint>

namespace kartgame {

// The Godot 4.7.1 audio boundary, measured. Issue #81, ADR-0035.
//
// This is the third engine-boundary probe in this project and it is built like
// the first two. `tools/verify/integration_probe.gd` (ADR-0032) measured force
// application, `tools/verify/contact_probe.gd` (ADR-0033) measured contacts, and
// each asks one question per case with **an analytic prediction printed beside
// every measurement**. Three of ADR-0033's seven answers contradicted what this
// project had written down as settled, which is the whole argument for measuring
// rather than reading.
//
// ## The question this file exists to settle
//
// Issue #81's title says `AudioStreamGenerator` and its acceptance criterion says
// "no locks or allocation on the audio thread". Those name two different
// architectures and `src/core/audio_state.h` says so in its own header comment:
//
//   (a) `AudioStreamGenerator` + `AudioStreamGeneratorPlayback::push_buffer`,
//       a ring filled from `_process` on the **main** thread, or
//   (b) a GDExtension `AudioStream` / `AudioStreamPlayback` pair overriding
//       `_mix`, which may or may not be called on a genuine audio thread.
//
// Under (a) there is no audio thread in user code at all, so "no locks on the
// audio thread" is vacuous and the real risk is underrun. Under (b) the criterion
// is load-bearing and the state handoff needs a real lock-free transport. Which
// one Godot provides is an engine behavior, so it is measured here.
//
// ## Why nothing at namespace scope here has a non-trivial constructor
//
// CLAUDE.md's trap: a namespace-scope `StringName` in a GDExtension crashes Godot
// in `dyld4::callInitializer`, because static initializers run inside `dlopen`
// before godot-cpp has bound its interface. Every `std::atomic` in `ProbeStats`
// is constant-initialized — the constructor is `constexpr` and emits no dynamic
// initializer — so the block is safe where a `StringName` is not. Every `String`
// and `StringName` in the .cpp is a function-local static or a local.

// Upper bound on the harmonic stack the probe renders.
//
// Not 24. `kz_audio::LADDER_MEASURED_TO` is 24 but `STACK_CEILING_HZ` is 8000,
// and the ceiling is the binding rule — at a 2,000 rpm idle the fundamental is
// 33.3 Hz and 8 kHz is the 240th harmonic. A cost measurement taken only at 24
// partials would understate the worst case by an order of magnitude, so the probe
// has to be able to render the worst case.
inline constexpr int AUDIO_PROBE_MAX_PARTIALS = 256;

// How many `_mix` calls are recorded individually before the probe stops storing.
// At 48 kHz and a 512-frame block that is 94 calls per second, so 8192 covers
// about 87 seconds — far longer than any run below.
inline constexpr int AUDIO_PROBE_CALL_CAPACITY = 8192;

// How many distinct thread ids `_mix` is allowed to be seen on before the probe
// stops recording new ones. Four, because the interesting answers are one
// (a dedicated mixer thread), "the main thread" (no audio thread at all), and
// "more than one" (a thread pool, which would change the transport).
inline constexpr int AUDIO_PROBE_MAX_THREADS = 4;

// Iterations of the integer calibration loop. 20,000 dependent 64-bit operations
// is 20-60 us depending on the core, which is a few tenths of a percent of an
// 11.6 ms mix block — visible to a timer, invisible to the audio device.
inline constexpr int AUDIO_PROBE_CALIB_ITERS = 20000;

// Mono scratch the real `EngineSynth` renders into before it is fanned out to
// `AudioFrame`. Sized well past the 512 frames `_mix` was measured to ask for, so
// that a driver with a larger buffer cannot make the audio thread allocate. A
// block larger than this renders in chunks rather than overflowing.
inline constexpr int AUDIO_PROBE_SCRATCH_FRAMES = 4096;

// Everything the audio thread writes and the main thread reads.
//
// A plain struct of atomics rather than a class with accessors, because every
// member is touched from `_mix` and the point of the file is that `_mix` does no
// allocation, takes no lock, and calls nothing that might.
struct ProbeStats {
	// --- identity -----------------------------------------------------------

	// Hash of `std::this_thread::get_id()` on the thread that armed the probe.
	std::atomic<uint64_t> main_thread_hash{ 0 };
	// `OS::get_thread_caller_id()` on the same thread, for cross-checking the
	// std hash against Godot's own numbering.
	std::atomic<uint64_t> main_thread_godot_id{ 0 };
	// Distinct `std::this_thread::get_id()` hashes seen inside `_mix`.
	std::atomic<uint64_t> mix_thread_hash[AUDIO_PROBE_MAX_THREADS]{};
	std::atomic<int32_t> mix_thread_count{ 0 };
	// `OS::get_thread_caller_id()` read from inside `_mix`, once, on the first
	// call. Read once rather than every call because it is a call into godot-cpp
	// from the audio thread and the cost of that is not what is being measured.
	std::atomic<uint64_t> mix_godot_id{ 0 };
	std::atomic<int32_t> mix_godot_id_read{ 0 };

	// --- call pattern -------------------------------------------------------

	std::atomic<int64_t> mix_calls{ 0 };
	std::atomic<int64_t> mix_frames_total{ 0 };
	std::atomic<int32_t> recorded{ 0 };
	// Per-call block size, interval since the previous call, and the wall-clock
	// the synth itself took. Written only while `recorded < CAPACITY`, so the
	// arrays hold the first N calls in order and a reader needs no lock.
	std::atomic<int32_t> call_frames[AUDIO_PROBE_CALL_CAPACITY]{};
	std::atomic<int64_t> call_interval_ns[AUDIO_PROBE_CALL_CAPACITY]{};
	std::atomic<int64_t> call_synth_ns[AUDIO_PROBE_CALL_CAPACITY]{};
	// An integer-only workload of a fixed iteration count, run in the same `_mix`
	// call as the synth. It exists to tell two explanations of a slow audio thread
	// apart: if this is slowed by the same factor as the floating-point synth, the
	// thread is simply running on a slower core and every kind of DSP pays it; if
	// it is not, only floating point is affected and the answer would be different.
	// Measuring the difference rather than asserting one is the whole point.
	std::atomic<int64_t> call_calib_ns[AUDIO_PROBE_CALL_CAPACITY]{};
	std::atomic<int64_t> last_mix_ns{ 0 };

	// --- concurrency --------------------------------------------------------

	// Number of `_mix` calls in flight. Its maximum answers "is the callback
	// re-entrant"; a value above 0 seen from `_physics_process` answers "does it
	// run concurrently with the physics tick".
	std::atomic<int32_t> mix_active{ 0 };
	std::atomic<int32_t> mix_reentrant{ 0 };
	// Set for the duration of the busy window `_physics_process` opens.
	std::atomic<int32_t> physics_window{ 0 };
	// `_mix` entered or left while a physics window was open.
	std::atomic<int64_t> mix_saw_physics{ 0 };
	// Samples taken inside the physics window, and how many of them found a mix
	// in flight. The ratio is the measurement; the raw counts are printed so a
	// reader can see the sample size.
	std::atomic<int64_t> physics_samples{ 0 };
	std::atomic<int64_t> physics_saw_mix{ 0 };
	std::atomic<int64_t> physics_ticks{ 0 };
	std::atomic<int64_t> physics_ticks_overlapped{ 0 };

	// --- the transport experiment ------------------------------------------

	// A seqlock over a real `kart::core::EngineAudioInput`, written by
	// `_physics_process` and read by `_mix`. Every field is derived from one
	// counter, so a torn read is detectable rather than theoretical.
	std::atomic<uint32_t> seq{ 0 };
	kart::core::EngineAudioInput seq_payload{};
	std::atomic<int64_t> seq_reads{ 0 };
	std::atomic<int64_t> seq_retries{ 0 };
	std::atomic<int64_t> seq_torn{ 0 };
	// Reads that exhausted the retry budget and returned an unvalidated snapshot.
	//
	// Counted apart from `seq_torn` because the two are different defects. A
	// validated seqlock read that is torn would mean the algorithm is wrong; a read
	// that gave up means the retry bound is too low for the writer's rate, which is
	// a tuning fact about `--torture` and not about the transport. Merging them
	// reported 1022 "torn seqlock reads" that were nothing of the kind.
	std::atomic<int64_t> seq_gave_up{ 0 };

	// The same payload copied with no synchronization at all, in the same `_mix`
	// call, as the control. If this never tears the concurrency finding is not
	// what it looks like.
	kart::core::EngineAudioInput naked_payload{};
	std::atomic<int64_t> naked_reads{ 0 };
	std::atomic<int64_t> naked_torn{ 0 };

	// --- configuration the audio thread reads -------------------------------

	// Cached on the main thread before `play()`, so `_mix` never calls
	// `AudioServer::get_mix_rate()`. The point of the probe is that `_mix` does
	// nothing a real synth would not be allowed to do.
	std::atomic<double> mix_rate{ 48000.0 };
	std::atomic<int32_t> partials{ 24 };
	std::atomic<double> fundamental_hz{ 216.666666666 };
	std::atomic<double> gain{ 0.05 };

	// How many times `_mix` reads each payload per call, and whether the physics
	// window publishes continuously instead of once per tick.
	//
	// Both default to the honest rate — one read per block, one publish per tick —
	// because that is the configuration the answer is about. `--torture` raises
	// them to show that the unsynchronized copy *can* tear, which is a different
	// question from whether it did in eight seconds, and one that a count of zero
	// on its own cannot tell apart from safety.
	std::atomic<int32_t> reads_per_mix{ 1 };
	std::atomic<int32_t> torture{ 0 };

	// True to render the stack from the interpolated table instead of `std::sin`.
	// Exposed because the offline benchmark shows the two differ by 3-4x and the
	// audio thread is not the main thread, so extrapolating one from the other is
	// exactly the "conclusion resting on a model" issue #107 is the worked example
	// of. Both get measured where they will actually run.
	std::atomic<int32_t> use_table{ 0 };

	// --- the real synth, on the real audio thread ----------------------------
	//
	// Everything above measures a *stand-in* stack: a bare phase accumulator and a
	// gain, which is the cheapest thing that has the right shape. That was the
	// right instrument for ADR-0035, whose question was about the transport.
	//
	// It is the wrong instrument for the question that follows it. `EngineSynth`
	// does per-partial work the stand-in does not — two ladders crossfaded, a
	// per-partial gain ramp, a comb, a noise layer, combustion jitter — and its
	// partial count is chosen from a frequency ceiling rather than passed in, so
	// `active_partials()` returns 192 at a 2,000 rpm idle and 40 at 12,000. **Idle
	// is the worst case and it is 8x the 24 partials ADR-0035 costed.** Multiplying
	// that measurement by eight is a model, and issue #107 is what this project
	// calls the practice of trusting one. So the real class renders in the real
	// `_mix` instead.
	std::atomic<int32_t> use_engine_synth{ 0 };

	// Crank speed the synth is held at, rpm. The operating point, and the only
	// input that changes the partial count.
	std::atomic<double> synth_rpm{ 0.0 };

	// Load, 0..1, and the trailing-throttle flag. Both change which ladder is in
	// use and therefore the per-partial arithmetic, so a cost figure taken at one
	// of them is not a cost figure for the other.
	std::atomic<double> synth_load{ 1.0 };
	std::atomic<int32_t> synth_trailing{ 0 };

	// What `active_partials()` actually chose, read back from `_mix`. The point of
	// the exercise: a cost per frame means nothing without the partial count it was
	// paid for, and that count is the synth's decision rather than the caller's.
	std::atomic<int32_t> synth_partials{ 0 };

	// Sample rate the synth was configured at, echoed so the report can divide by
	// the right frame period rather than by an assumed one.
	std::atomic<double> synth_rate{ 0.0 };

	// --- the scrub and wind layers, same treatment ---------------------------
	//
	// Issue #84's layers cost something and #152 records that there is not much
	// room: the harmonic stack is already 69% of §15's audio row at idle and
	// twelve voices are 74.76% of real time. A new layer per kart lands on top of
	// that, so it gets measured where it will run before it gets built into a
	// scene — the same order ADR-0035 used, and for the same reason.
	//
	// **A separate mode rather than a flag on the engine one**, because the two
	// have to be attributable separately. Summed into one timing there would be no
	// way to answer "what did scrub add", which is the only question that matters
	// for the twelve-kart budget.
	//
	// Unlike the harmonic stack, these have no partial count and no operating point
	// that changes their arithmetic: a band-pass is nine multiply-adds whatever its
	// cutoff is, and `std::pow` and `std::tan` are per block. The sweep therefore
	// exists to *confirm* that flatness rather than to find a worst case, and a
	// sweep that came back non-flat would itself be the finding.
	std::atomic<int32_t> use_scrub_wind{ 0 };

	// Render the wind layer too, and sum it. Set together with `use_scrub_wind` to
	// get the cost of what a player's kart actually runs; cleared to attribute the
	// scrub layer alone, which is what an opponent's kart costs.
	std::atomic<int32_t> scrub_wind_include_wind{ 0 };

	// The operating point. `EngineAudioInput`'s three scrub fields and nothing
	// else, because nothing else in that struct reaches either layer.
	std::atomic<double> scrub_drive{ 0.0 };
	std::atomic<double> scrub_speed_ms{ 0.0 };
	std::atomic<int32_t> scrub_surface{ 0 };
};

ProbeStats &audio_probe_stats();

// The playback half of architecture (b). One question: which thread calls this,
// how often, with how many frames, and what does it cost.
class AudioProbePlayback : public godot::AudioStreamPlayback {
	GDCLASS(AudioProbePlayback, godot::AudioStreamPlayback)

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

private:
	bool _active = false;
	double _position = 0.0;
	// Per-partial phase, carried across blocks. A synth that reset its phase per
	// block would click at every block boundary, which is a different bug from
	// the one being measured here, so the probe does it properly.
	double _phase[AUDIO_PROBE_MAX_PARTIALS] = {};

	// The real thing, when `use_engine_synth` is set. A member rather than a
	// pointer so that no allocation happens anywhere near `_mix`, and configured in
	// `_instantiate_playback` — which is provably the main thread, being called out
	// of `AudioStreamPlayer::play()` — rather than in `_start`, whose thread this
	// probe has never measured and therefore does not get to assume.
	kart::core::EngineSynth _synth;
	float _scratch[AUDIO_PROBE_SCRATCH_FRAMES] = {};

	// Issue #84's two layers, same arrangement and for the same reasons: members
	// so `_mix` allocates nothing, configured from `_instantiate_playback` because
	// `configure` resets a filter and reseeds an RNG and is not an audio-thread
	// call. A second scratch so the wind layer can be rendered and summed without
	// either layer having to accumulate into the other's buffer — which would
	// measure an add that the production path, where they are separate streams,
	// never performs.
	kart::core::ScrubSynth _scrub;
	kart::core::WindSynth _wind;
	float _scratch_wind[AUDIO_PROBE_SCRATCH_FRAMES] = {};

public:
	// Size the synth to a device rate and clear it. Main thread only: it fills a
	// 4,096-point sine table and both ladder tables.
	void configure_synth(double p_sample_rate);
};

// The stream half of architecture (b). Exists only to hand back a playback.
class AudioProbeStream : public godot::AudioStream {
	GDCLASS(AudioProbeStream, godot::AudioStream)

protected:
	static void _bind_methods();

public:
	virtual godot::Ref<godot::AudioStreamPlayback> _instantiate_playback() const override;
	virtual godot::String _get_stream_name() const override;
	virtual double _get_length() const override;
	virtual bool _is_monophonic() const override;
};

// The main-thread half: arms the probe, opens the physics busy window that
// question 4 needs, runs the offline cost benchmarks, and reads the stats out.
//
// A `Node` and not a `RefCounted` because `_physics_process` is the only way to
// get a callback on the physics thread at the physics rate, and that callback is
// half of the overlap measurement.
class AudioProbe : public godot::Node {
	GDCLASS(AudioProbe, godot::Node)

protected:
	static void _bind_methods();

public:
	void _ready() override;
	void _physics_process(double p_delta) override;

	// Reset every counter and record the calling thread as "main". Call before
	// `AudioStreamPlayer::play()`.
	void arm();

	void set_partials(int32_t p_partials);
	void set_mix_rate(double p_hz);
	void set_fundamental_hz(double p_hz);
	void set_gain(double p_gain);
	void set_use_table(bool p_use_table);
	void set_torture(bool p_torture);

	// Render the real `EngineSynth` inside `_mix` instead of the stand-in stack,
	// held at one operating point. `p_rpm` is the only lever that changes the
	// partial count; `p_load` and `p_trailing` change which ladder is being
	// crossfaded and so change the per-partial arithmetic.
	void set_engine_synth(bool p_enabled);
	void set_synth_operating_point(double p_rpm, double p_load, bool p_trailing);

	// Issue #84's layers, inside the same `_mix`. `p_include_wind` false costs an
	// opponent's kart (scrub only, positional); true costs the player's (scrub plus
	// the non-positional wind layer).
	void set_scrub_wind(bool p_enabled, bool p_include_wind);
	void set_scrub_wind_operating_point(double p_drive, double p_speed_ms, int32_t p_surface);

	// Microseconds `_physics_process` spends in its busy window, sampling
	// `mix_active`. Zero disables the window entirely.
	void set_physics_busy_us(int32_t p_us);

	godot::Dictionary report() const;
	godot::PackedInt32Array call_frames() const;
	godot::PackedInt64Array call_intervals_ns() const;
	godot::PackedInt64Array call_synth_ns() const;
	godot::PackedInt64Array call_calib_ns() const;

	// The same integer workload `_mix` runs, on the calling thread. Returns
	// nanoseconds for `AUDIO_PROBE_CALIB_ITERS` iterations.
	godot::Dictionary benchmark_int(int32_t p_reps) const;

	// Render `p_frames` frames of a `p_partials` sine stack at fundamental
	// `p_f0_hz`, `p_reps` times, on the calling thread. Returns nanoseconds per
	// frame for the `std::sin` implementation and for a 4096-point interpolated
	// table, because the ratio between them decides whether the §15 budget is a
	// constraint or a formality.
	//
	// The fundamental is a parameter and not a constant because a partial count
	// only makes sense next to one: 240 partials exist at a 2,000 rpm idle, where
	// the top of the stack is 8 kHz, and do not exist at 13,000 rpm, where the
	// 111th partial is already past Nyquist.
	godot::Dictionary benchmark_synth(int32_t p_partials, int32_t p_frames, int32_t p_reps, double p_f0_hz) const;

	// Facts about publishing `kart::core::EngineAudioInput` by value: its size,
	// whether `std::atomic` over it is lock-free, and the uncontended cost of a
	// seqlock publish and read.
	godot::Dictionary transport_facts(int32_t p_reps) const;

private:
	int32_t _busy_us = 0;
	// Monotonic counter every field of the seqlock payload is derived from.
	uint32_t _publish_counter = 0;
};

} // namespace kartgame

#endif // KARTGAME_AUDIO_AUDIO_PROBE_H
