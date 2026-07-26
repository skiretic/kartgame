#ifndef KART_CORE_SCRUB_WIND_H
#define KART_CORE_SCRUB_WIND_H

#include "core/audio_state.h"
#include "core/kz_audio_reference.h"
#include "core/pcg32.h"
#include "core/surface.h"
#include "core/units.h"

#include <cmath>

// The two layers `engine_synth.h` deliberately refused to build: tire scrub and
// wind. ARCHITECTURE.md §12, issue #84.
//
// Nothing here may include godot-cpp. ADR-0017. `src/audio/noise_voice.{h,cpp}`
// is the join to Godot's mixer and this file has no opinion about it, exactly as
// `engine_synth.h` has none about `engine_voice.cpp`.
//
// ## What is sourced here and what is not, stated before the code
//
// `kz_audio::SCRUB_SPECTRUM_MEASURED` and `kz_audio::WIND_SPECTRUM_MEASURED` are
// both false and `docs/REFERENCES.md` records the search behind them. So this
// file is split down the middle on purpose, and the split is the design:
//
//   **The drive is solver truth.** `EngineAudioInput::scrub` is the mean of the
//   four corners' slip angles normalized against the tire's own peak, computed in
//   `kart_body.cpp` from `WheelTelemetry`. `speed_ms` is road speed. `surface` is
//   the `SurfaceType` a wheel is actually on, and `surface.h`'s `roughness` column
//   exists for this file by name — its comment says so. None of that is invented.
//
//   **The timbre is not.** Center frequency, bandwidth, the wind cutoff, the
//   speed exponent: no recording in this project's corpus isolates a tire or the
//   airflow from an engine running over the top of it. §5 item 10 forbids
//   inventing a spectrum and calling it sourced, so every number in
//   `scrub_wind_tuning` below is a declared guess and every one of them is a row
//   in `core/tuning.h` marked `Provenance::Unsourced`, adjustable on F2 from the
//   first drive.
//
// The consequence a reader should take away: **a wrong number in this file is a
// knob somebody has not turned yet, not a defect.** A wrong number in
// `kz_audio_reference.h` would be a defect. That is the whole reason the two
// files are separate.
//
// ## Why two classes and not one
//
// They go to different places. Scrub comes off the tires and has to be locatable
// by ear when it is another kart's — M8's acceptance criterion names exactly
// that — so it is a positional emitter on the kart. Wind is at the driver's head,
// is not a source in the world at all, and would be nonsense to attenuate with
// distance or to Doppler-shift. One class rendering both into one buffer would
// force the two into a single spatialization, and the only way back would be to
// split it later anyway.
//
// It also gives issue #160 what it asks for: turning one layer off must not
// require re-judging the others, which is only true if they are separable at the
// mixer rather than summed here.
//
// ## The block/tick rate mismatch is `audio_state.h`'s and it applies unchanged
//
// State arrives at 120 Hz, samples leave at the device rate, and a mix block is
// roughly 1.39 solver ticks. Both classes therefore ramp their gains across the
// block rather than stepping them at its top. Filter *coefficients* are updated
// once per block and not ramped: a cutoff step is inaudible where a level step
// is a click, and recomputing a `std::tan` per sample would be the most expensive
// thing in either class by an order of magnitude.

namespace kart::core {

// The knobs. Every one of them is a guess with the guess labeled, and each says
// what to listen for instead of citing something that does not exist.
//
// Grouped in their own namespace for the reason `synth_tuning` is: so a reader
// can see at a glance how much of this file is chosen. The answer here is "all of
// the timbre and none of the drive", which is the opposite balance to
// `engine_synth.h` and is the honest state of the sourcing.
namespace scrub_wind_tuning {

// --- scrub ----------------------------------------------------------------------

// Where the scrub band sits on a smooth surface, Hz, and how narrow it is.
//
// **Unsourced.** Stick-slip of a tread band is a resonance, so a band-pass rather
// than a shelf is the right *shape* whatever the numbers are — that much follows
// from the mechanism and not from a measurement. The center and the Q do not.
//
// Listen for: too high and it is a whistle rather than a squeal; too low and it
// disappears under the engine's own noise layer. Too much Q and it rings like a
// sine on every corner; too little and it stops being distinguishable from wind.
inline constexpr double SCRUB_CENTER_HZ = 900.0;
inline constexpr double SCRUB_Q = 2.4;

// Where the band moves to on a fully rough surface — `Surface::roughness` of 1.
// Interpolated linearly against that column, which is dimensionless by design so
// that nobody mistakes it for a texture depth.
//
// **Unsourced**, but the direction is not arbitrary: a surface that shears inside
// the terrain rather than at the rubber cannot sustain a tread-band resonance, so
// it has to broaden and drop. Grass and dirt are within 6% of each other on grip
// and `surface.h` says in as many words that they are told apart by sound. This
// is the parameter that does the telling.
//
// Listen for: running onto grass should read as a rumble and not as a quieter
// squeal.
inline constexpr double SCRUB_ROUGH_CENTER_HZ = 320.0;
inline constexpr double SCRUB_ROUGH_Q = 0.8;

// Shaping exponent on the 0..1 drive.
//
// **Unsourced.** Above 1 the layer stays quiet until the tire is genuinely past
// its peak and then comes in fast; below 1 it is audible from the first degree of
// slip. #84's acceptance criterion is that scrub signals grip loss *before* the
// visuals do, which argues for the low end of the range — but a layer that is
// always on signals nothing, so the two pull against each other and only an ear
// settles it.
inline constexpr double SCRUB_GAMMA = 1.6;

// Road speed at which the scrub layer reaches full level, m/s.
//
// The *shape* here is derived rather than guessed, and it is the one piece of
// this file's amplitude law that is: the acoustic power a sliding contact patch
// radiates goes with the power it dissipates, which is friction force times slip
// velocity, and slip velocity at a fixed slip angle is proportional to road speed.
// So level has to rise with speed and has to be zero at a standstill. A kart held
// against the brakes with the wheels turned is silent, and any law that does not
// say so is wrong.
//
// **The reference speed itself is unsourced** — it is where the ramp saturates,
// not whether there is one. 12 m/s is 43 km/h, roughly a slow hairpin.
//
// Listen for: a low-speed spin that squeals as loudly as a 100 km/h slide means
// this is too low.
inline constexpr double SCRUB_FULL_SPEED_MS = 12.0;

// --- wind -----------------------------------------------------------------------

// The wind layer's low-pass corner, as Hz per m/s of road speed, plus a floor.
//
// Same split as the scrub ramp above: **the proportionality is derived and the
// constant is not.** Turbulent noise from a bluff body peaks at a Strouhal
// frequency f = St*U/L — the spectrum's corner moves with speed over a fixed
// geometry, so wind at 120 km/h is not the same noise louder, it is brighter as
// well. Folding St and L into one number is what makes this a single tunable;
// what that number is for a kart driver's helmet in a kart's wake is unmeasured.
//
// Listen for: if the layer's character does not change between a hairpin exit and
// the end of the straight, this is too small.
inline constexpr double WIND_CUTOFF_HZ_PER_MS = 26.0;
inline constexpr double WIND_CUTOFF_FLOOR_HZ = 90.0;

// Level law: `(speed / WIND_REFERENCE_SPEED_MS) ^ WIND_SPEED_EXPONENT`, so the
// reference speed is where the layer is at unity and the exponent is how fast it
// gets there.
//
// **Both unsourced.** An aeroacoustic dipole radiates power with U^6, giving an
// amplitude exponent of 3, and that is the number a first guess reaches for — but
// it describes a source in free field, not a pressure fluctuation measured inside
// the boundary layer at an ear, and the two are not the same quantity. Naming the
// dipole law here would be exactly the "plausible-sounding intuition" §5 item 10
// exists to stop, so 2.0 is carried as a guess rather than 3.0 as a derivation.
//
// 38 m/s is 137 km/h — the measured top speed, so unity is flat out.
//
// Listen for: wind that masks the engine anywhere in the range is wrong, #84 says
// so explicitly. Wind that is inaudible at the end of the straight is also wrong.
inline constexpr double WIND_REFERENCE_SPEED_MS = 38.0;
inline constexpr double WIND_SPEED_EXPONENT = 2.0;

// --- both -----------------------------------------------------------------------

// PCG32 streams. ARCHITECTURE.md §8 rule 3: every user carries its own explicitly
// seeded stream, because a shared generator makes what you hear depend on how many
// other things drew from it. These two must also differ from `EngineSynth`'s three
// or the engine's noise layer and the scrub layer would be correlated, which
// sounds like a filter sweep on one source rather than two sources.
inline constexpr uint64_t SCRUB_STREAM = 0xD6E8FEB86659FD93ULL;
inline constexpr uint64_t WIND_STREAM = 0xA24BAED4963EE407ULL;

// How fast a gain is allowed to move, as the fraction of the remaining distance
// closed per sample at 48 kHz. Applied on top of the per-block ramp for the
// *drive*, not for the config gains.
//
// The drive needs it and the config gains do not: `scrub` is a mean over four
// corners of a slip angle, and a wheel that loses contact drops out of that mean
// entirely, which is a step in the drive between one tick and the next with no
// step in what the kart is doing. Ramping only across the block would still let a
// single tick's dropout through as a 8.3 ms amplitude notch. This is a one-pole
// on top, and 0.0006 is a time constant of about 35 ms.
//
// **Unsourced**, and it is a smoothing constant rather than a spectrum, so it is
// the one number here that would not be upgraded by a recording.
inline constexpr double DRIVE_SMOOTHING_ALPHA = 0.0006;

} // namespace scrub_wind_tuning

// A topology-preserving state variable filter, one channel.
//
// Chosen over a biquad because both layers move their cutoff — the wind corner
// with speed and the scrub band with surface — and a direct-form biquad is not
// well behaved when its coefficients move under it. This form's states are the two
// integrator outputs, which stay meaningful across a coefficient change, so a
// cutoff that moves once per block does not thump.
//
// `set` calls `std::tan` and is a per-block call. `process` is the per-sample one
// and is nine multiply-adds with no branch and no libm.
struct Svf {
	// Cutoff in Hz, resonance as Q. Both clamped: a cutoff at or above Nyquist
	// makes `tan` diverge, and a Q at zero is a divide by zero.
	void set(double cutoff_hz, double q, double sample_rate) {
		double fc = cutoff_hz;
		if (fc < 1.0) {
			fc = 1.0;
		}
		// 0.45 of the rate rather than 0.5: `tan` at the limit is unbounded and the
		// last few percent of the band buys nothing either layer can use.
		if (fc > 0.45 * sample_rate) {
			fc = 0.45 * sample_rate;
		}
		double qq = q;
		if (qq < 0.05) {
			qq = 0.05;
		}
		const double g = std::tan(PI * fc / sample_rate);
		const double k = 1.0 / qq;
		a1_ = 1.0 / (1.0 + g * (g + k));
		a2_ = g * a1_;
		a3_ = g * a2_;
	}

	// One sample in, low-pass and band-pass out. Both are produced because both
	// layers want one of the two and producing the other is free — the band-pass is
	// an intermediate of the low-pass.
	void process(double in, double &lowpass, double &bandpass) {
		const double v3 = in - ic2_;
		const double v1 = a1_ * ic1_ + a2_ * v3;
		const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
		ic1_ = 2.0 * v1 - ic1_;
		ic2_ = 2.0 * v2 - ic2_;
		lowpass = v2;
		bandpass = v1;
	}

	void reset() {
		ic1_ = 0.0;
		ic2_ = 0.0;
	}

private:
	double a1_ = 0.0;
	double a2_ = 0.0;
	double a3_ = 0.0;
	double ic1_ = 0.0;
	double ic2_ = 0.0;
};

// The knobs a scene sets, as distinct from the ones only an ear can. Same split
// and same reasoning as `EngineAudioConfig`, which this deliberately mirrors:
// levels here, timbre in `scrub_wind_tuning`.
struct ScrubWindConfig {
	// Overall output gain, linear, per layer. Placeholders in exactly the sense
	// `EngineAudioConfig::gain` is — set low so a first drive cannot be painful,
	// with nothing yet to be low relative to. **Issue #160 owns making these mean
	// something**, and it is blocked on this file existing.
	double scrub_gain = 0.30;
	double wind_gain = 0.20;
};

// Tire scrub: band-passed noise, modulated by slip, weighted by road speed,
// re-centered by surface.
//
// One instance per kart. Positional — see the header comment.
class ScrubSynth {
public:
	ScrubSynth() {
		configure(ScrubWindConfig(), 48000.0);
	}

	// Not an audio-thread call: it resets the filter and reseeds the RNG.
	void configure(const ScrubWindConfig &config, double sample_rate) {
		config_ = config;
		sample_rate_ = sample_rate > 1.0 ? sample_rate : 48000.0;
		reset();
	}

	// The one knob that moves while the synth runs, for the same reason
	// `EngineSynth::set_noise_gain` is: it is a per-sample multiplier, so a change
	// between blocks is a step in a gain and not in a phase.
	void set_gain(double gain) { config_.scrub_gain = gain > 0.0 ? gain : 0.0; }
	double gain() const { return config_.scrub_gain; }

	void reset() {
		filter_.reset();
		rng_.reseed(0, scrub_wind_tuning::SCRUB_STREAM);
		level_ = 0.0;
	}

	// Publish one tick of state. Physics thread.
	void publish(const EngineAudioInput &input) { input_ = input; }

	// Render `frames` mono samples. Audio thread. Adds nothing and allocates
	// nothing; it writes.
	void render(float *out, int frames) {
		if (frames <= 0) {
			return;
		}

		// Surface first: it sets where the band sits, and it is a per-block update.
		const Surface &surf = surface(input_.surface);
		const double rough = surf.roughness < 0.0 ? 0.0 : (surf.roughness > 1.0 ? 1.0 : surf.roughness);
		const double center = scrub_wind_tuning::SCRUB_CENTER_HZ +
				rough * (scrub_wind_tuning::SCRUB_ROUGH_CENTER_HZ - scrub_wind_tuning::SCRUB_CENTER_HZ);
		const double q = scrub_wind_tuning::SCRUB_Q +
				rough * (scrub_wind_tuning::SCRUB_ROUGH_Q - scrub_wind_tuning::SCRUB_Q);
		filter_.set(center, q, sample_rate_);

		// The target level. Drive shaped by gamma, times the speed weight the
		// dissipated-power argument requires, times the layer gain.
		double drive = input_.scrub;
		drive = drive < 0.0 ? 0.0 : (drive > 1.0 ? 1.0 : drive);
		const double shaped = std::pow(drive, scrub_wind_tuning::SCRUB_GAMMA);

		double speed_weight = input_.speed_ms / scrub_wind_tuning::SCRUB_FULL_SPEED_MS;
		speed_weight = speed_weight < 0.0 ? 0.0 : (speed_weight > 1.0 ? 1.0 : speed_weight);

		const double target = shaped * speed_weight * config_.scrub_gain;

		// The drive one-pole, scaled to this device rate so the time constant is a
		// time and not a sample count.
		const double alpha = drive_alpha(sample_rate_);

		for (int i = 0; i < frames; ++i) {
			level_ += (target - level_) * alpha;
			const double white = rng_.next_double() * 2.0 - 1.0;
			double lp = 0.0;
			double bp = 0.0;
			filter_.process(white, lp, bp);
			out[i] = static_cast<float>(bp * level_);
		}
	}

	// For the tests and the HUD: the level the layer settled at, 0..1-ish.
	double level() const { return level_; }

private:
	static double drive_alpha(double sample_rate) {
		// 0.0006 is declared at 48 kHz. At another rate the same time constant is a
		// different per-sample fraction, and getting that wrong makes the smoothing
		// device-dependent — which is the shape of a bug nobody finds until somebody
		// runs at 44.1 kHz.
		const double a = scrub_wind_tuning::DRIVE_SMOOTHING_ALPHA * 48000.0 / sample_rate;
		return a > 1.0 ? 1.0 : a;
	}

	ScrubWindConfig config_{};
	EngineAudioInput input_{};
	Svf filter_{};
	Pcg32 rng_{ 0, scrub_wind_tuning::SCRUB_STREAM };
	double sample_rate_ = 48000.0;
	double level_ = 0.0;
};

// Wind: low-passed noise whose corner and level both rise with road speed.
//
// One instance, for the local kart only. Non-positional — see the header comment.
class WindSynth {
public:
	WindSynth() {
		configure(ScrubWindConfig(), 48000.0);
	}

	void configure(const ScrubWindConfig &config, double sample_rate) {
		config_ = config;
		sample_rate_ = sample_rate > 1.0 ? sample_rate : 48000.0;
		reset();
	}

	void set_gain(double gain) { config_.wind_gain = gain > 0.0 ? gain : 0.0; }
	double gain() const { return config_.wind_gain; }

	void reset() {
		filter_.reset();
		rng_.reseed(0, scrub_wind_tuning::WIND_STREAM);
		level_ = 0.0;
	}

	void publish(const EngineAudioInput &input) { input_ = input; }

	void render(float *out, int frames) {
		if (frames <= 0) {
			return;
		}

		double speed = input_.speed_ms;
		speed = speed < 0.0 ? 0.0 : speed;

		const double cutoff = scrub_wind_tuning::WIND_CUTOFF_FLOOR_HZ +
				scrub_wind_tuning::WIND_CUTOFF_HZ_PER_MS * speed;
		// A gentle Q. The corner is a spectral slope and not a resonance: a peak
		// here would read as a whistle at one specific speed, which is a defect that
		// only shows up while driving.
		filter_.set(cutoff, 0.707, sample_rate_);

		const double ratio = speed / scrub_wind_tuning::WIND_REFERENCE_SPEED_MS;
		const double target = std::pow(ratio, scrub_wind_tuning::WIND_SPEED_EXPONENT) *
				config_.wind_gain;

		// Ramped across the block only. Wind has no per-corner dropout to smooth —
		// road speed is continuous by construction — so the extra one-pole the scrub
		// layer needs would only add lag here.
		const double step = (target - level_) / static_cast<double>(frames);

		for (int i = 0; i < frames; ++i) {
			level_ += step;
			const double white = rng_.next_double() * 2.0 - 1.0;
			double lp = 0.0;
			double bp = 0.0;
			filter_.process(white, lp, bp);
			out[i] = static_cast<float>(lp * level_);
		}
		level_ = target;
	}

	double level() const { return level_; }

private:
	ScrubWindConfig config_{};
	EngineAudioInput input_{};
	Svf filter_{};
	Pcg32 rng_{ 0, scrub_wind_tuning::WIND_STREAM };
	double sample_rate_ = 48000.0;
	double level_ = 0.0;
};

} // namespace kart::core

#endif // KART_CORE_SCRUB_WIND_H
