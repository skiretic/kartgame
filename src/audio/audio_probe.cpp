#include "audio/audio_probe.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

using namespace godot;

namespace kartgame {

namespace {

// Two pi, spelled out because the phase accumulator wraps against it every frame
// and a `Math_TAU` from a header that might change is not worth the dependency.
constexpr double TAU = 6.283185307179586476925286766559;

// Points in the interpolated sine table. 4096 is the size a real synth would
// reach for: the worst-case interpolation error of a linear-interpolated table
// of N points is (pi/N)^2/2, which at 4096 is 2.9e-7 — about -131 dBFS, two
// orders of magnitude below the 24-bit noise floor and therefore inaudible.
constexpr int SIN_TABLE_POINTS = 4096;

// How many times a seqlock read may retry before giving up and returning an
// unvalidated snapshot. Bounded on purpose: an unbounded spin on the audio thread
// is a lock by another name, which is the thing issue #81 forbids. 64 is roughly
// 300 ns of spinning against a 120 Hz writer whose publish window is 3.5 ns wide,
// so a real boundary never reaches it.
constexpr int64_t SEQ_RETRY_BUDGET = 64;

// --- the shared statistics block -------------------------------------------
//
// Namespace scope, and safe there for a reason worth stating: every member is a
// `std::atomic` of a scalar, whose constructor is `constexpr`, so the whole block
// is constant-initialized and emits no dynamic initializer. CLAUDE.md's
// `dyld4::callInitializer` crash comes from namespace-scope objects whose
// constructors *run* inside `dlopen` — a `StringName` calls through godot-cpp's
// not-yet-bound interface table. This does not.
ProbeStats g_stats;

// Per-partial amplitudes and the sine table. Filled on the main thread by
// `arm()`; only read from `_mix`.
double g_partial_gain[AUDIO_PROBE_MAX_PARTIALS] = {};
float g_sin_table[SIN_TABLE_POINTS + 1] = {};
bool g_tables_ready = false;

int64_t now_ns() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count();
}

// Stop the optimizer deleting a benchmark loop whose result it can prove nobody
// reads. Without this the seqlock timings below came back as 0.0 ns per
// operation, which is not a fast transport — it is no transport at all, and it
// looked exactly like a good result.
template <typename T>
inline void keep(T &value) {
#if defined(__clang__) || defined(__GNUC__)
	asm volatile("" : "+r,m"(value) : : "memory");
#else
	volatile T sink = value;
	(void)sink;
#endif
}

inline void keep_memory(void *p) {
#if defined(__clang__) || defined(__GNUC__)
	asm volatile("" : : "r,m"(p) : "memory");
#else
	(void)p;
#endif
}

uint64_t this_thread_hash() {
	return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

// A ladder in the measured range, as a closed form.
//
// -2.95 dB per doubling of harmonic number is `kz_audio::DECAY_PUBLISHED_PIPE`,
// so the amplitude of harmonic n is 10^(-2.95*log2(n)/20) = n^-0.4900. It is not
// read from the header on purpose: the real synth owns that choice, the two
// published and fitted slopes for the pipe ladder disagree, and this probe must
// not look like a second opinion on a question it is not asking.
//
// **The cost measurement does not depend on this at all** — every partial is one
// `sin`, one multiply and one add whatever its gain is. Using a slope from the
// measured range rather than the 1/n a synthesizer defaults to just keeps the
// probe from quietly becoming the thing `kz_audio_reference.h` exists to warn
// against.
double partial_gain(int n) {
	return std::pow(static_cast<double>(n), -2.95 / (20.0 * 0.301029995663981));
}

void build_tables() {
	if (g_tables_ready) {
		return;
	}
	for (int n = 0; n < AUDIO_PROBE_MAX_PARTIALS; ++n) {
		g_partial_gain[n] = partial_gain(n + 1);
	}
	for (int i = 0; i <= SIN_TABLE_POINTS; ++i) {
		g_sin_table[i] = static_cast<float>(std::sin(TAU * static_cast<double>(i) / SIN_TABLE_POINTS));
	}
	g_tables_ready = true;
}

// Render one block of a `partials`-deep sine stack with `std::sin`.
//
// Phase per partial is carried in `phase[]` across blocks, which is the only way
// a block-based synth avoids a click at every boundary. `dphi` is the
// fundamental's radians per sample; harmonic n advances n times as fast.
//
// Returns nothing and writes both channels the same, because the question is cost
// and panning is not part of it.
void render_sin(double *phase, int partials, double dphi, double gain, AudioFrame *out, int frames) {
	for (int i = 0; i < frames; ++i) {
		double sum = 0.0;
		for (int n = 0; n < partials; ++n) {
			sum += g_partial_gain[n] * std::sin(phase[n]);
			phase[n] += dphi * static_cast<double>(n + 1);
			// A `while` and not an `if`. Harmonic n advances n times as fast as the
			// fundamental, so above harmonic `rate/(2*f0)` — 110 at 48 kHz and a
			// 13,000 rpm fundamental — one subtraction does not bring it back into
			// range. That is above Nyquist and inaudible, but a table lookup indexed
			// off an unwrapped phase reads past the end of the table, which is a
			// segfault and not a wrong note. It cost one crash to find.
			while (phase[n] >= TAU) {
				phase[n] -= TAU;
			}
		}
		const float v = static_cast<float>(sum * gain);
		out[i].left = v;
		out[i].right = v;
	}
}

// The same stack from a linearly interpolated table. Same phase bookkeeping, same
// memory traffic; the only difference is the transcendental.
void render_table(double *phase, int partials, double dphi, double gain, AudioFrame *out, int frames) {
	constexpr double SCALE = static_cast<double>(SIN_TABLE_POINTS) / TAU;
	for (int i = 0; i < frames; ++i) {
		double sum = 0.0;
		for (int n = 0; n < partials; ++n) {
			const double x = phase[n] * SCALE;
			int idx = static_cast<int>(x);
			// Belt and braces against the crash the `while` below fixes: the table
			// has SIN_TABLE_POINTS+1 entries so that idx == SIN_TABLE_POINTS is a
			// legal read, and anything past that is clamped rather than trusted.
			if (idx < 0 || idx >= SIN_TABLE_POINTS) {
				idx = 0;
			}
			const double frac = x - static_cast<double>(idx);
			const double a = g_sin_table[idx];
			const double b = g_sin_table[idx + 1];
			sum += g_partial_gain[n] * (a + (b - a) * frac);
			phase[n] += dphi * static_cast<double>(n + 1);
			while (phase[n] >= TAU) {
				phase[n] -= TAU;
			}
		}
		const float v = static_cast<float>(sum * gain);
		out[i].left = v;
		out[i].right = v;
	}
}

// A dependent chain of 64-bit integer operations. Dependent on purpose: an
// independent chain would measure the core's issue width rather than its clock,
// and the question here is how fast the thread's core is running.
uint64_t calib_int(uint64_t seed, int iters) {
	uint64_t x = seed;
	for (int i = 0; i < iters; ++i) {
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		x += 0x9E3779B97F4A7C15ull;
	}
	return x;
}

// --- the seqlock payload ----------------------------------------------------
//
// Every field of `EngineAudioInput` is derived from one counter `k`, so a reader
// that gets a consistent snapshot sees one `k` in all eleven fields and a reader
// that catches the writer mid-publish sees two. That turns "is this transport
// safe" from an argument into a count.

void fill_payload(kart::core::EngineAudioInput &p, uint32_t k) {
	const double d = static_cast<double>(k);
	p.rpm = d;
	p.load = d;
	p.throttle = d;
	p.clutch_slip = d;
	p.speed_ms = d;
	p.scrub = d;
	p.gear = static_cast<int>(k & 0xFFFF);
	p.surface = static_cast<int>(k & 0xFFFF);
	p.trailing = (k & 1u) != 0u;
	p.shifting = (k & 2u) != 0u;
	p.over_rev = (k & 4u) != 0u;
	p.on_limiter = (k & 8u) != 0u;
}

// True when every field agrees on one counter value.
bool payload_consistent(const kart::core::EngineAudioInput &p) {
	const double d = p.rpm;
	if (p.load != d || p.throttle != d || p.clutch_slip != d || p.speed_ms != d || p.scrub != d) {
		return false;
	}
	const uint32_t k = static_cast<uint32_t>(d);
	if (p.gear != static_cast<int>(k & 0xFFFF) || p.surface != static_cast<int>(k & 0xFFFF)) {
		return false;
	}
	if (p.trailing != ((k & 1u) != 0u) || p.shifting != ((k & 2u) != 0u)) {
		return false;
	}
	if (p.over_rev != ((k & 4u) != 0u) || p.on_limiter != ((k & 8u) != 0u)) {
		return false;
	}
	return true;
}

// Remember a thread id if it is new. Linear scan of four slots, which is cheaper
// than any container and is the point — this runs on the audio thread.
void record_thread(uint64_t hash) {
	const int32_t count = g_stats.mix_thread_count.load(std::memory_order_acquire);
	for (int32_t i = 0; i < count && i < AUDIO_PROBE_MAX_THREADS; ++i) {
		if (g_stats.mix_thread_hash[i].load(std::memory_order_relaxed) == hash) {
			return;
		}
	}
	if (count >= AUDIO_PROBE_MAX_THREADS) {
		return;
	}
	// A second mixer thread racing here would at worst record a duplicate, which
	// the report prints as-is rather than deduplicating — a duplicate slot is
	// itself evidence of two threads.
	const int32_t slot = g_stats.mix_thread_count.fetch_add(1, std::memory_order_acq_rel);
	if (slot < AUDIO_PROBE_MAX_THREADS) {
		g_stats.mix_thread_hash[slot].store(hash, std::memory_order_release);
	}
}

} // namespace

ProbeStats &audio_probe_stats() {
	return g_stats;
}

// --- AudioProbePlayback -----------------------------------------------------

void AudioProbePlayback::_bind_methods() {}

void AudioProbePlayback::_start(double p_from_pos) {
	_active = true;
	_position = p_from_pos;
	for (int i = 0; i < AUDIO_PROBE_MAX_PARTIALS; ++i) {
		_phase[i] = 0.0;
	}
}

void AudioProbePlayback::_stop() {
	_active = false;
}

bool AudioProbePlayback::_is_playing() const {
	return _active;
}

int32_t AudioProbePlayback::_get_loop_count() const {
	return 0;
}

double AudioProbePlayback::_get_playback_position() const {
	return _position;
}

void AudioProbePlayback::_seek(double p_position) {
	_position = p_position;
}

int32_t AudioProbePlayback::_mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) {
	// `p_rate_scale` is the pitch-scale the player asks for. Ignored deliberately:
	// a synthesized engine note computes its own frequency and has no source rate
	// to resample from, so honoring it would be wrong rather than missing.
	(void)p_rate_scale;

	const int64_t entry_ns = now_ns();
	ProbeStats &s = g_stats;

	record_thread(this_thread_hash());

	// `OS::get_thread_caller_id()` read exactly once, from whichever thread gets
	// here first. It is a call into godot-cpp from a possibly-audio thread, which
	// is the sort of thing a real synth must not do per call, so the probe does
	// not do it per call either — one read is enough to name the thread.
	int32_t expected = 0;
	if (s.mix_godot_id_read.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
		OS *os = OS::get_singleton();
		if (os != nullptr) {
			s.mix_godot_id.store(os->get_thread_caller_id(), std::memory_order_release);
		}
	}

	// Question 4, half one: is another `_mix` already in flight?
	const int32_t before = s.mix_active.fetch_add(1, std::memory_order_acq_rel);
	if (before > 0) {
		s.mix_reentrant.fetch_add(1, std::memory_order_relaxed);
	}
	const bool physics_at_entry = s.physics_window.load(std::memory_order_acquire) != 0;

	// The transport experiment. A seqlock read of the payload the physics tick is
	// publishing, and — as the control — the same struct copied with nothing
	// protecting it at all.
	const int32_t reads_per_mix = s.reads_per_mix.load(std::memory_order_relaxed);
	for (int32_t read_index = 0; read_index < reads_per_mix; ++read_index) {
		kart::core::EngineAudioInput snapshot{};
		int64_t retries = 0;
		bool validated = false;
		for (;;) {
			const uint32_t s1 = s.seq.load(std::memory_order_acquire);
			if ((s1 & 1u) != 0u) {
				++retries;
				if (retries > SEQ_RETRY_BUDGET) {
					break;
				}
				continue;
			}
			std::memcpy(&snapshot, &s.seq_payload, sizeof(snapshot));
			std::atomic_thread_fence(std::memory_order_acquire);
			const uint32_t s2 = s.seq.load(std::memory_order_relaxed);
			if (s1 == s2) {
				validated = true;
				break;
			}
			++retries;
			// A bounded retry, because an unbounded one on the audio thread is a
			// lock by another name. 64 is far more than a 120 Hz writer can force —
			// and far less than `--torture`'s continuous writer can, which is why
			// giving up is counted separately below instead of being called a tear.
			if (retries > SEQ_RETRY_BUDGET) {
				break;
			}
		}
		s.seq_reads.fetch_add(1, std::memory_order_relaxed);
		s.seq_retries.fetch_add(retries, std::memory_order_relaxed);
		if (!validated) {
			s.seq_gave_up.fetch_add(1, std::memory_order_relaxed);
		} else if (!payload_consistent(snapshot)) {
			// A validated seqlock read that is torn would mean the algorithm is
			// wrong. This counter existing and staying at zero is the point.
			s.seq_torn.fetch_add(1, std::memory_order_relaxed);
		}

		// The control. This is a genuine data race and it is meant to be: the
		// question "does the state handoff need a real transport" is only answered
		// by showing that the naive one tears on this hardware, at this rate.
		kart::core::EngineAudioInput naked{};
		std::memcpy(&naked, &s.naked_payload, sizeof(naked));
		s.naked_reads.fetch_add(1, std::memory_order_relaxed);
		if (!payload_consistent(naked)) {
			s.naked_torn.fetch_add(1, std::memory_order_relaxed);
		}
	}

	const int64_t previous_ns = s.last_mix_ns.exchange(entry_ns, std::memory_order_relaxed);
	const int64_t interval_ns = (previous_ns == 0) ? 0 : (entry_ns - previous_ns);

	const int partials = static_cast<int>(s.partials.load(std::memory_order_relaxed));
	const double rate = s.mix_rate.load(std::memory_order_relaxed);
	const double f0 = s.fundamental_hz.load(std::memory_order_relaxed);
	const double gain = s.gain.load(std::memory_order_relaxed);
	const double dphi = TAU * f0 / rate;

	const int64_t synth_start_ns = now_ns();
	if (s.use_scrub_wind.load(std::memory_order_relaxed) != 0) {
		// Issue #84's layers, at a held operating point, in the production call
		// sequence: `publish` is a plain store and `render` is the audio-thread call.
		//
		// Checked before the engine-synth branch so the two are never timed together
		// -- the whole reason this is a separate mode is that "what did scrub add"
		// has to be answerable, and a summed figure cannot answer it.
		kart::core::EngineAudioInput input;
		input.scrub = s.scrub_drive.load(std::memory_order_relaxed);
		input.speed_ms = s.scrub_speed_ms.load(std::memory_order_relaxed);
		input.surface = static_cast<int>(s.scrub_surface.load(std::memory_order_relaxed));
		_scrub.publish(input);
		_wind.publish(input);

		const bool with_wind = s.scrub_wind_include_wind.load(std::memory_order_relaxed) != 0;

		int32_t done = 0;
		while (done < p_frames) {
			int32_t chunk = p_frames - done;
			if (chunk > AUDIO_PROBE_SCRATCH_FRAMES) {
				chunk = AUDIO_PROBE_SCRATCH_FRAMES;
			}
			_scrub.render(_scratch, chunk);
			if (with_wind) {
				_wind.render(_scratch_wind, chunk);
			}
			for (int32_t i = 0; i < chunk; ++i) {
				// Summed here only so both renders reach the device and neither can be
				// optimized away. In production these are two separate streams on two
				// players -- scrub positional, wind not -- and this add does not exist.
				const float sample = with_wind ? (_scratch[i] + _scratch_wind[i]) : _scratch[i];
				p_buffer[done + i].left = sample;
				p_buffer[done + i].right = sample;
			}
			done += chunk;
		}
	} else if (s.use_engine_synth.load(std::memory_order_relaxed) != 0) {
		// The real class, at a held operating point. `publish` is a plain store and
		// `render` is documented as the audio-thread call, so this is the production
		// call sequence and not an imitation of it.
		//
		// The state is assembled here rather than seqlocked in because the question
		// is what `render` costs, and a transport already measured at 4.6 ns per
		// block would be 0.04% of the answer while making the operating point
		// depend on a second thread's timing.
		kart::core::EngineAudioInput input;
		input.rpm = s.synth_rpm.load(std::memory_order_relaxed);
		input.load = s.synth_load.load(std::memory_order_relaxed);
		input.throttle = input.load;
		input.trailing = s.synth_trailing.load(std::memory_order_relaxed) != 0;
		input.gear = 2;
		_synth.publish(input);

		// Chunked against the scratch, so a device asking for a larger block than
		// the 512 this was measured at degrades into two renders rather than into a
		// buffer overrun.
		int32_t done = 0;
		while (done < p_frames) {
			int32_t chunk = p_frames - done;
			if (chunk > AUDIO_PROBE_SCRATCH_FRAMES) {
				chunk = AUDIO_PROBE_SCRATCH_FRAMES;
			}
			_synth.render(_scratch, chunk);
			for (int32_t i = 0; i < chunk; ++i) {
				const float sample = _scratch[i];
				p_buffer[done + i].left = sample;
				p_buffer[done + i].right = sample;
			}
			done += chunk;
		}
		// The count is the synth's own decision, so it is read back rather than
		// assumed. Without it a nanosecond figure has no denominator.
		s.synth_partials.store(static_cast<int32_t>(_synth.partial_count()), std::memory_order_relaxed);
	} else if (partials > 0 && g_tables_ready) {
		if (s.use_table.load(std::memory_order_relaxed) != 0) {
			render_table(_phase, partials, dphi, gain, p_buffer, p_frames);
		} else {
			render_sin(_phase, partials, dphi, gain, p_buffer, p_frames);
		}
	} else {
		for (int32_t i = 0; i < p_frames; ++i) {
			p_buffer[i].left = 0.0f;
			p_buffer[i].right = 0.0f;
		}
	}
	const int64_t synth_ns = now_ns() - synth_start_ns;

	const int64_t calib_start_ns = now_ns();
	const uint64_t calib = calib_int(static_cast<uint64_t>(entry_ns), AUDIO_PROBE_CALIB_ITERS);
	const int64_t calib_ns = now_ns() - calib_start_ns;
	// Keep the result alive so the loop cannot be folded away, and write it
	// somewhere harmless. The low bit of a chaotic 64-bit value is not audio, so
	// it is added at -300 dBFS and cannot be heard at any gain.
	p_buffer[0].left += static_cast<float>(calib & 1u) * 1e-15f;

	const int32_t slot = s.recorded.fetch_add(1, std::memory_order_relaxed);
	if (slot < AUDIO_PROBE_CALL_CAPACITY) {
		s.call_frames[slot].store(p_frames, std::memory_order_relaxed);
		s.call_interval_ns[slot].store(interval_ns, std::memory_order_relaxed);
		s.call_synth_ns[slot].store(synth_ns, std::memory_order_relaxed);
		s.call_calib_ns[slot].store(calib_ns, std::memory_order_relaxed);
	}

	s.mix_calls.fetch_add(1, std::memory_order_relaxed);
	s.mix_frames_total.fetch_add(p_frames, std::memory_order_relaxed);
	_position += static_cast<double>(p_frames) / rate;

	const bool physics_at_exit = s.physics_window.load(std::memory_order_acquire) != 0;
	if (physics_at_entry || physics_at_exit) {
		s.mix_saw_physics.fetch_add(1, std::memory_order_relaxed);
	}
	s.mix_active.fetch_sub(1, std::memory_order_acq_rel);

	return p_frames;
}

void AudioProbePlayback::configure_synth(double p_sample_rate) {
	kart::core::EngineAudioConfig config;
	_synth.configure(config, p_sample_rate > 1.0 ? p_sample_rate : 48000.0);
	g_stats.synth_rate.store(_synth.nyquist_hz() * 2.0, std::memory_order_relaxed);

	// Issue #84's layers, sized to the same device rate. Unconditional rather than
	// gated on `use_scrub_wind`, because this runs on the main thread out of
	// `_instantiate_playback` and the mode may be armed after the player is playing.
	// A synth configured lazily inside `_mix` would be exactly the allocation this
	// whole file exists to prove does not happen.
	const kart::core::ScrubWindConfig sw_config;
	_scrub.configure(sw_config, p_sample_rate > 1.0 ? p_sample_rate : 48000.0);
	_wind.configure(sw_config, p_sample_rate > 1.0 ? p_sample_rate : 48000.0);
}

// --- AudioProbeStream -------------------------------------------------------

void AudioProbeStream::_bind_methods() {}

Ref<AudioStreamPlayback> AudioProbeStream::_instantiate_playback() const {
	Ref<AudioProbePlayback> playback;
	playback.instantiate();
	// Sized here, on the main thread, from the rate the caller cached before
	// `play()`. `configure` fills a 4,096-point sine table and both ladders, which
	// is exactly the kind of work that must not happen on the audio thread.
	playback->configure_synth(g_stats.mix_rate.load(std::memory_order_relaxed));
	return playback;
}

String AudioProbeStream::_get_stream_name() const {
	return String("AudioProbeStream");
}

double AudioProbeStream::_get_length() const {
	// Zero means "unbounded" to the player, which is what a live synth is.
	return 0.0;
}

bool AudioProbeStream::_is_monophonic() const {
	return true;
}

// --- AudioProbe -------------------------------------------------------------

void AudioProbe::_bind_methods() {
	ClassDB::bind_method(D_METHOD("arm"), &AudioProbe::arm);
	ClassDB::bind_method(D_METHOD("set_partials", "partials"), &AudioProbe::set_partials);
	ClassDB::bind_method(D_METHOD("set_mix_rate", "hz"), &AudioProbe::set_mix_rate);
	ClassDB::bind_method(D_METHOD("set_fundamental_hz", "hz"), &AudioProbe::set_fundamental_hz);
	ClassDB::bind_method(D_METHOD("set_gain", "gain"), &AudioProbe::set_gain);
	ClassDB::bind_method(D_METHOD("set_use_table", "use_table"), &AudioProbe::set_use_table);
	ClassDB::bind_method(D_METHOD("set_torture", "torture"), &AudioProbe::set_torture);
	ClassDB::bind_method(D_METHOD("set_engine_synth", "enabled"), &AudioProbe::set_engine_synth);
	ClassDB::bind_method(D_METHOD("set_synth_operating_point", "rpm", "load", "trailing"),
			&AudioProbe::set_synth_operating_point);
	ClassDB::bind_method(D_METHOD("set_physics_busy_us", "us"), &AudioProbe::set_physics_busy_us);
	ClassDB::bind_method(D_METHOD("report"), &AudioProbe::report);
	ClassDB::bind_method(D_METHOD("call_frames"), &AudioProbe::call_frames);
	ClassDB::bind_method(D_METHOD("call_intervals_ns"), &AudioProbe::call_intervals_ns);
	ClassDB::bind_method(D_METHOD("call_synth_ns"), &AudioProbe::call_synth_ns);
	ClassDB::bind_method(D_METHOD("set_scrub_wind", "enabled", "include_wind"),
			&AudioProbe::set_scrub_wind);
	ClassDB::bind_method(D_METHOD("set_scrub_wind_operating_point", "drive", "speed_ms", "surface"),
			&AudioProbe::set_scrub_wind_operating_point);
	ClassDB::bind_method(D_METHOD("call_calib_ns"), &AudioProbe::call_calib_ns);
	ClassDB::bind_method(D_METHOD("benchmark_int", "reps"), &AudioProbe::benchmark_int);
	ClassDB::bind_method(D_METHOD("benchmark_synth", "partials", "frames", "reps", "f0_hz"), &AudioProbe::benchmark_synth);
	ClassDB::bind_method(D_METHOD("transport_facts", "reps"), &AudioProbe::transport_facts);
}

void AudioProbe::arm() {
	build_tables();

	ProbeStats &s = g_stats;
	s.main_thread_hash.store(this_thread_hash(), std::memory_order_relaxed);
	OS *os = OS::get_singleton();
	s.main_thread_godot_id.store(os != nullptr ? os->get_thread_caller_id() : 0, std::memory_order_relaxed);

	for (int i = 0; i < AUDIO_PROBE_MAX_THREADS; ++i) {
		s.mix_thread_hash[i].store(0, std::memory_order_relaxed);
	}
	s.mix_thread_count.store(0, std::memory_order_relaxed);
	s.mix_godot_id.store(0, std::memory_order_relaxed);
	s.mix_godot_id_read.store(0, std::memory_order_relaxed);

	s.mix_calls.store(0, std::memory_order_relaxed);
	s.mix_frames_total.store(0, std::memory_order_relaxed);
	s.recorded.store(0, std::memory_order_relaxed);
	s.last_mix_ns.store(0, std::memory_order_relaxed);

	s.mix_active.store(0, std::memory_order_relaxed);
	s.mix_reentrant.store(0, std::memory_order_relaxed);
	s.physics_window.store(0, std::memory_order_relaxed);
	s.mix_saw_physics.store(0, std::memory_order_relaxed);
	s.physics_samples.store(0, std::memory_order_relaxed);
	s.physics_saw_mix.store(0, std::memory_order_relaxed);
	s.physics_ticks.store(0, std::memory_order_relaxed);
	s.physics_ticks_overlapped.store(0, std::memory_order_relaxed);

	s.seq.store(0, std::memory_order_relaxed);
	fill_payload(s.seq_payload, 0);
	fill_payload(s.naked_payload, 0);
	s.seq_reads.store(0, std::memory_order_relaxed);
	s.seq_retries.store(0, std::memory_order_relaxed);
	s.seq_torn.store(0, std::memory_order_relaxed);
	s.seq_gave_up.store(0, std::memory_order_relaxed);
	s.naked_reads.store(0, std::memory_order_relaxed);
	s.naked_torn.store(0, std::memory_order_relaxed);

	// Not `use_engine_synth`, `synth_rpm`, `synth_load` or `synth_trailing`: those
	// are the operating point the caller sets *before* arming, and clearing them
	// here would silently measure a synth at zero rpm. `synth_partials` is a
	// read-back and does get cleared, so that a stale count from a previous case
	// cannot be mistaken for this one's.
	s.synth_partials.store(0, std::memory_order_relaxed);

	_publish_counter = 0;
}

void AudioProbe::set_engine_synth(bool p_enabled) {
	g_stats.use_engine_synth.store(p_enabled ? 1 : 0, std::memory_order_relaxed);
}

void AudioProbe::set_synth_operating_point(double p_rpm, double p_load, bool p_trailing) {
	g_stats.synth_rpm.store(p_rpm > 0.0 ? p_rpm : 0.0, std::memory_order_relaxed);
	g_stats.synth_load.store(p_load < 0.0 ? 0.0 : (p_load > 1.0 ? 1.0 : p_load), std::memory_order_relaxed);
	g_stats.synth_trailing.store(p_trailing ? 1 : 0, std::memory_order_relaxed);
}

void AudioProbe::set_scrub_wind(bool p_enabled, bool p_include_wind) {
	g_stats.use_scrub_wind.store(p_enabled ? 1 : 0, std::memory_order_relaxed);
	g_stats.scrub_wind_include_wind.store(p_include_wind ? 1 : 0, std::memory_order_relaxed);
}

void AudioProbe::set_scrub_wind_operating_point(double p_drive, double p_speed_ms, int32_t p_surface) {
	const double drive = p_drive < 0.0 ? 0.0 : (p_drive > 1.0 ? 1.0 : p_drive);
	g_stats.scrub_drive.store(drive, std::memory_order_relaxed);
	g_stats.scrub_speed_ms.store(p_speed_ms > 0.0 ? p_speed_ms : 0.0, std::memory_order_relaxed);
	// Clamped to the enum's range here rather than trusted, because `surface()` in
	// `core/surface.h` falls back to asphalt for an out-of-range value and a probe
	// silently measuring asphalt while its report says grass is the kind of thing
	// that gets quoted for a milestone.
	const int32_t surface = (p_surface >= 0 && p_surface < kart::core::SURFACE_COUNT)
			? p_surface
			: static_cast<int32_t>(kart::core::SURFACE_ASPHALT);
	g_stats.scrub_surface.store(surface, std::memory_order_relaxed);
}

void AudioProbe::set_partials(int32_t p_partials) {
	g_stats.partials.store(p_partials < 0 ? 0 : (p_partials > AUDIO_PROBE_MAX_PARTIALS ? AUDIO_PROBE_MAX_PARTIALS : p_partials),
			std::memory_order_relaxed);
}

void AudioProbe::set_mix_rate(double p_hz) {
	g_stats.mix_rate.store(p_hz > 1.0 ? p_hz : 48000.0, std::memory_order_relaxed);
}

void AudioProbe::set_fundamental_hz(double p_hz) {
	g_stats.fundamental_hz.store(p_hz, std::memory_order_relaxed);
}

void AudioProbe::set_gain(double p_gain) {
	g_stats.gain.store(p_gain, std::memory_order_relaxed);
}

void AudioProbe::set_use_table(bool p_use_table) {
	g_stats.use_table.store(p_use_table ? 1 : 0, std::memory_order_relaxed);
}

void AudioProbe::set_torture(bool p_torture) {
	g_stats.torture.store(p_torture ? 1 : 0, std::memory_order_relaxed);
	// 2048 reads per block is about 40 us of an 11.6 ms block, so the audio device
	// never notices, and it raises the reader's exposure by three orders of
	// magnitude — which is what it takes to observe an event whose analytic
	// probability is around 1e-6 per read.
	g_stats.reads_per_mix.store(p_torture ? 2048 : 1, std::memory_order_relaxed);
}

void AudioProbe::set_physics_busy_us(int32_t p_us) {
	_busy_us = p_us < 0 ? 0 : p_us;
}

void AudioProbe::_ready() {
	// Same reason `KartBody::_ready` does it: Godot enables physics processing for
	// an overridden `_physics_process` automatically, but only through the very
	// `NOTIFICATION_READY` this override is part of. Asked for explicitly, because
	// a probe whose overlap window silently never opens reports "no concurrency"
	// and looks exactly like a probe that worked.
	set_physics_process(true);
}

void AudioProbe::_physics_process(double p_delta) {
	(void)p_delta;
	ProbeStats &s = g_stats;
	s.physics_ticks.fetch_add(1, std::memory_order_relaxed);

	// Publish one tick of engine state twice: once through a seqlock and once
	// through nothing at all. Both writers run here, on the physics thread, at the
	// physics rate, which is exactly where the real boundary would publish from.
	++_publish_counter;
	const uint32_t k = _publish_counter;

	const uint32_t seq0 = s.seq.load(std::memory_order_relaxed);
	s.seq.store(seq0 + 1, std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_release);
	fill_payload(s.seq_payload, k);
	std::atomic_thread_fence(std::memory_order_release);
	s.seq.store(seq0 + 2, std::memory_order_relaxed);

	// The control, written field by field with compiler barriers between so that
	// the write cannot be coalesced into two vector stores and the window a reader
	// can land in is representative of a real struct assignment.
	fill_payload(s.naked_payload, k);
	std::atomic_signal_fence(std::memory_order_seq_cst);

	if (_busy_us <= 0) {
		return;
	}

	const bool torture = s.torture.load(std::memory_order_relaxed) != 0;

	// The overlap window. Spin for a known wall-clock duration with a flag raised,
	// sampling whether a `_mix` is in flight. Two counts come out: the fraction of
	// samples that caught one, which estimates the mix duty cycle, and the fraction
	// of ticks that caught one at all, which is the answer to "can these two run at
	// the same time".
	s.physics_window.store(1, std::memory_order_release);
	const int64_t until_ns = now_ns() + static_cast<int64_t>(_busy_us) * 1000;
	int64_t samples = 0;
	int64_t saw = 0;
	while (now_ns() < until_ns) {
		++samples;
		if (s.mix_active.load(std::memory_order_acquire) > 0) {
			++saw;
		}
		if (torture) {
			// Publish continuously for the whole window instead of once per tick.
			// This is not what the real boundary would do; it is how the question
			// "can an unsynchronized publish tear" is separated from "did it tear
			// in eight seconds", which a count of zero cannot do on its own.
			++_publish_counter;
			const uint32_t tk = _publish_counter;
			const uint32_t ts0 = s.seq.load(std::memory_order_relaxed);
			s.seq.store(ts0 + 1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			fill_payload(s.seq_payload, tk);
			std::atomic_thread_fence(std::memory_order_release);
			s.seq.store(ts0 + 2, std::memory_order_relaxed);
			fill_payload(s.naked_payload, tk);
			std::atomic_signal_fence(std::memory_order_seq_cst);
		}
	}
	s.physics_window.store(0, std::memory_order_release);

	s.physics_samples.fetch_add(samples, std::memory_order_relaxed);
	s.physics_saw_mix.fetch_add(saw, std::memory_order_relaxed);
	if (saw > 0) {
		s.physics_ticks_overlapped.fetch_add(1, std::memory_order_relaxed);
	}
}

Dictionary AudioProbe::report() const {
	const ProbeStats &s = g_stats;
	Dictionary d;

	d["main_thread_hash"] = static_cast<int64_t>(s.main_thread_hash.load(std::memory_order_relaxed));
	d["main_thread_godot_id"] = static_cast<int64_t>(s.main_thread_godot_id.load(std::memory_order_relaxed));
	d["mix_godot_id"] = static_cast<int64_t>(s.mix_godot_id.load(std::memory_order_relaxed));

	PackedInt64Array threads;
	const int32_t thread_count = s.mix_thread_count.load(std::memory_order_relaxed);
	for (int32_t i = 0; i < thread_count && i < AUDIO_PROBE_MAX_THREADS; ++i) {
		threads.push_back(static_cast<int64_t>(s.mix_thread_hash[i].load(std::memory_order_relaxed)));
	}
	d["mix_thread_hashes"] = threads;

	d["mix_calls"] = s.mix_calls.load(std::memory_order_relaxed);
	d["mix_frames_total"] = s.mix_frames_total.load(std::memory_order_relaxed);
	d["recorded"] = s.recorded.load(std::memory_order_relaxed);
	d["mix_reentrant"] = s.mix_reentrant.load(std::memory_order_relaxed);
	d["mix_saw_physics"] = s.mix_saw_physics.load(std::memory_order_relaxed);
	d["physics_ticks"] = s.physics_ticks.load(std::memory_order_relaxed);
	d["physics_ticks_overlapped"] = s.physics_ticks_overlapped.load(std::memory_order_relaxed);
	d["physics_samples"] = s.physics_samples.load(std::memory_order_relaxed);
	d["physics_saw_mix"] = s.physics_saw_mix.load(std::memory_order_relaxed);

	d["seq_reads"] = s.seq_reads.load(std::memory_order_relaxed);
	d["seq_retries"] = s.seq_retries.load(std::memory_order_relaxed);
	d["seq_torn"] = s.seq_torn.load(std::memory_order_relaxed);
	d["seq_gave_up"] = s.seq_gave_up.load(std::memory_order_relaxed);
	d["naked_reads"] = s.naked_reads.load(std::memory_order_relaxed);
	d["naked_torn"] = s.naked_torn.load(std::memory_order_relaxed);

	d["partials"] = s.partials.load(std::memory_order_relaxed);
	d["use_table"] = s.use_table.load(std::memory_order_relaxed) != 0;
	d["torture"] = s.torture.load(std::memory_order_relaxed) != 0;
	d["reads_per_mix"] = s.reads_per_mix.load(std::memory_order_relaxed);
	d["mix_rate"] = s.mix_rate.load(std::memory_order_relaxed);
	d["fundamental_hz"] = s.fundamental_hz.load(std::memory_order_relaxed);

	d["use_engine_synth"] = s.use_engine_synth.load(std::memory_order_relaxed) != 0;
	d["synth_rpm"] = s.synth_rpm.load(std::memory_order_relaxed);
	d["synth_load"] = s.synth_load.load(std::memory_order_relaxed);
	d["synth_trailing"] = s.synth_trailing.load(std::memory_order_relaxed) != 0;
	d["synth_partials"] = s.synth_partials.load(std::memory_order_relaxed);
	d["synth_rate"] = s.synth_rate.load(std::memory_order_relaxed);
	d["use_scrub_wind"] = s.use_scrub_wind.load(std::memory_order_relaxed) != 0;
	d["scrub_wind_include_wind"] = s.scrub_wind_include_wind.load(std::memory_order_relaxed) != 0;
	d["scrub_drive"] = s.scrub_drive.load(std::memory_order_relaxed);
	d["scrub_speed_ms"] = s.scrub_speed_ms.load(std::memory_order_relaxed);
	d["scrub_surface"] = s.scrub_surface.load(std::memory_order_relaxed);
	return d;
}

PackedInt32Array AudioProbe::call_frames() const {
	PackedInt32Array out;
	const int32_t n = g_stats.recorded.load(std::memory_order_acquire);
	const int32_t count = n < AUDIO_PROBE_CALL_CAPACITY ? n : AUDIO_PROBE_CALL_CAPACITY;
	for (int32_t i = 0; i < count; ++i) {
		out.push_back(g_stats.call_frames[i].load(std::memory_order_relaxed));
	}
	return out;
}

PackedInt64Array AudioProbe::call_intervals_ns() const {
	PackedInt64Array out;
	const int32_t n = g_stats.recorded.load(std::memory_order_acquire);
	const int32_t count = n < AUDIO_PROBE_CALL_CAPACITY ? n : AUDIO_PROBE_CALL_CAPACITY;
	for (int32_t i = 0; i < count; ++i) {
		out.push_back(g_stats.call_interval_ns[i].load(std::memory_order_relaxed));
	}
	return out;
}

PackedInt64Array AudioProbe::call_synth_ns() const {
	PackedInt64Array out;
	const int32_t n = g_stats.recorded.load(std::memory_order_acquire);
	const int32_t count = n < AUDIO_PROBE_CALL_CAPACITY ? n : AUDIO_PROBE_CALL_CAPACITY;
	for (int32_t i = 0; i < count; ++i) {
		out.push_back(g_stats.call_synth_ns[i].load(std::memory_order_relaxed));
	}
	return out;
}

PackedInt64Array AudioProbe::call_calib_ns() const {
	PackedInt64Array out;
	const int32_t n = g_stats.recorded.load(std::memory_order_acquire);
	const int32_t count = n < AUDIO_PROBE_CALL_CAPACITY ? n : AUDIO_PROBE_CALL_CAPACITY;
	for (int32_t i = 0; i < count; ++i) {
		out.push_back(g_stats.call_calib_ns[i].load(std::memory_order_relaxed));
	}
	return out;
}

Dictionary AudioProbe::benchmark_int(int32_t p_reps) const {
	Dictionary d;
	if (p_reps <= 0) {
		return d;
	}
	uint64_t x = 1;
	x = calib_int(x, AUDIO_PROBE_CALIB_ITERS);
	const int64_t start = now_ns();
	for (int32_t r = 0; r < p_reps; ++r) {
		x = calib_int(x, AUDIO_PROBE_CALIB_ITERS);
		keep(x);
	}
	const int64_t total = now_ns() - start;
	d["iters"] = AUDIO_PROBE_CALIB_ITERS;
	d["reps"] = p_reps;
	d["ns_per_rep"] = static_cast<double>(total) / static_cast<double>(p_reps);
	d["sink"] = static_cast<int64_t>(x & 0xFFFF);
	return d;
}

Dictionary AudioProbe::benchmark_synth(int32_t p_partials, int32_t p_frames, int32_t p_reps, double p_f0_hz) const {
	build_tables();

	Dictionary d;
	if (p_partials <= 0 || p_frames <= 0 || p_reps <= 0) {
		return d;
	}
	if (p_partials > AUDIO_PROBE_MAX_PARTIALS) {
		p_partials = AUDIO_PROBE_MAX_PARTIALS;
	}

	std::vector<AudioFrame> buffer(static_cast<size_t>(p_frames));
	double phase[AUDIO_PROBE_MAX_PARTIALS] = {};
	const double rate = g_stats.mix_rate.load(std::memory_order_relaxed);
	const double f0 = p_f0_hz > 0.0 ? p_f0_hz : g_stats.fundamental_hz.load(std::memory_order_relaxed);
	const double dphi = TAU * f0 / rate;

	// One untimed pass first. The first call touches 256 doubles of phase, a
	// 16 kB sine table and the output buffer for the first time; timing it would
	// be measuring the cache, not the synth.
	render_sin(phase, p_partials, dphi, 0.05, buffer.data(), p_frames);
	const int64_t sin_start = now_ns();
	for (int32_t r = 0; r < p_reps; ++r) {
		render_sin(phase, p_partials, dphi, 0.05, buffer.data(), p_frames);
	}
	const int64_t sin_ns = now_ns() - sin_start;

	render_table(phase, p_partials, dphi, 0.05, buffer.data(), p_frames);
	const int64_t table_start = now_ns();
	for (int32_t r = 0; r < p_reps; ++r) {
		render_table(phase, p_partials, dphi, 0.05, buffer.data(), p_frames);
	}
	const int64_t table_ns = now_ns() - table_start;

	// Kept live so the optimizer cannot delete the whole loop. It cannot, because
	// `buffer` escapes through `data()`, but saying so costs nothing.
	double sink = 0.0;
	for (int32_t i = 0; i < p_frames; ++i) {
		sink += buffer[i].left;
	}

	const double total_frames = static_cast<double>(p_frames) * static_cast<double>(p_reps);
	d["partials"] = p_partials;
	d["frames"] = p_frames;
	d["reps"] = p_reps;
	d["f0_hz"] = f0;
	d["sin_total_ns"] = sin_ns;
	d["table_total_ns"] = table_ns;
	d["sin_ns_per_frame"] = static_cast<double>(sin_ns) / total_frames;
	d["table_ns_per_frame"] = static_cast<double>(table_ns) / total_frames;
	d["sink"] = sink;
	return d;
}

Dictionary AudioProbe::transport_facts(int32_t p_reps) const {
	Dictionary d;
	d["sizeof_engine_audio_input"] = static_cast<int64_t>(sizeof(kart::core::EngineAudioInput));
	d["alignof_engine_audio_input"] = static_cast<int64_t>(alignof(kart::core::EngineAudioInput));
	d["atomic_u64_always_lock_free"] = std::atomic<uint64_t>::is_always_lock_free;
	// The number that decides the transport: `std::atomic<T>` over a struct this
	// size is not lock-free on any mainstream ABI, so publishing the struct by
	// value through one would take a lock — on the audio thread, which is the one
	// thing issue #81's acceptance criterion forbids.
	d["atomic_payload_always_lock_free"] = std::atomic<kart::core::EngineAudioInput>::is_always_lock_free;

	if (p_reps <= 0) {
		return d;
	}

	// Uncontended cost of one seqlock publish and one seqlock read, so the
	// transport can be priced against the 0.5 ms budget rather than assumed cheap.
	kart::core::EngineAudioInput scratch{};
	std::atomic<uint32_t> seq{ 0 };
	kart::core::EngineAudioInput payload{};

	const int64_t write_start = now_ns();
	for (int32_t i = 0; i < p_reps; ++i) {
		const uint32_t s0 = seq.load(std::memory_order_relaxed);
		seq.store(s0 + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		fill_payload(payload, static_cast<uint32_t>(i));
		keep_memory(&payload);
		std::atomic_thread_fence(std::memory_order_release);
		seq.store(s0 + 2, std::memory_order_relaxed);
	}
	const int64_t write_ns = now_ns() - write_start;

	double read_sink = 0.0;
	const int64_t read_start = now_ns();
	for (int32_t i = 0; i < p_reps; ++i) {
		for (;;) {
			const uint32_t s1 = seq.load(std::memory_order_acquire);
			if ((s1 & 1u) != 0u) {
				continue;
			}
			std::memcpy(&scratch, &payload, sizeof(scratch));
			std::atomic_thread_fence(std::memory_order_acquire);
			if (seq.load(std::memory_order_relaxed) == s1) {
				break;
			}
		}
		read_sink += scratch.rpm;
		keep(read_sink);
	}
	const int64_t read_ns = now_ns() - read_start;

	d["seq_publish_ns"] = static_cast<double>(write_ns) / static_cast<double>(p_reps);
	d["seq_read_ns"] = static_cast<double>(read_ns) / static_cast<double>(p_reps);
	d["sink"] = read_sink;
	return d;
}

} // namespace kartgame
