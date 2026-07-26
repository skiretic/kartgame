#include "doctest.h"

#include "core/audio_state.h"
#include "core/scrub_wind.h"
#include "core/surface.h"
#include "core/tuning.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// What can honestly be asserted about two layers whose spectra are unmeasured.
//
// `docs/REFERENCES.md` records that no recording isolates a tire or the airflow
// from an engine, so `SCRUB_SPECTRUM_MEASURED` and `WIND_SPECTRUM_MEASURED` are
// both false and there is no ladder to compare against the way
// `test_engine_synth.cpp` compares one. Asserting "the band is at 900 Hz" would
// only be asserting that the constant equals itself.
//
// So what is tested here is the half that *is* constrained — the drive law and
// the filter's realized behavior:
//
//   * silence when the kart is not moving, whatever the slip angle says. This is
//     the dissipated-power argument in `scrub_wind.h` and it is the one amplitude
//     claim in either layer that is derived rather than guessed;
//   * the scrub band's measured center and -3 dB width really are where the
//     coefficients were asked to put them, so a wrong number is a wrong knob and
//     never a broken filter;
//   * the wind corner really moves with speed, which is the Strouhal
//     proportionality and is the other derived claim;
//   * monotonicity of both level laws, because "louder when sliding" and "louder
//     when fast" are the two things a driver would notice were inverted;
//   * that no operating point writes a sample outside full scale, which the scrub
//     layer did at its shipped gain for two milestones;
//   * that the wind layer's realized dB per doubling of road speed is the figure
//     `kz_audio::WIND_DB_PER_SPEED_DOUBLING` cites and not that figure plus
//     whatever its own low-pass was contributing;
//   * no click across a block boundary, and no dependence on the block size,
//     which is `audio_state.h`'s rate-mismatch rule applied to these classes; and
//   * bit-identical output for identical input, which is the same determinism
//     property the rest of `src/core/` is held to.
//
// These tests print their measurements, for the same reason the engine synth's do:
// a number that only exists inside a CHECK is a number nobody can quote in an
// acceptance comment.

using kart::core::EngineAudioInput;
using kart::core::ScrubSynth;
using kart::core::ScrubWindConfig;
using kart::core::Svf;
using kart::core::WindSynth;
namespace tuning = kart::core::scrub_wind_tuning;

namespace {

constexpr double SAMPLE_RATE = 48000.0;

// RMS of a buffer. The layers are noise, so RMS is the only meaningful level:
// a peak is one draw of a PRNG and says nothing.
double rms(const std::vector<float> &samples) {
	if (samples.empty()) {
		return 0.0;
	}
	double sum = 0.0;
	for (const float sample : samples) {
		sum += static_cast<double>(sample) * static_cast<double>(sample);
	}
	return std::sqrt(sum / static_cast<double>(samples.size()));
}

// Relative difference between two positive quantities.
//
// Used in place of `doctest::Approx(x).epsilon(e)` throughout this file, and the
// reason is a trap worth naming: doctest's Approx compares against
// `epsilon * (scale + max(|a|, |b|))` with `scale` defaulting to **1.0**. Every
// level in this file is a few hundredths, so that additive 1.0 dominates and an
// `epsilon(0.06)` on a 0.03 RMS is a tolerance of 0.062 -- six thousand times
// looser than it reads, and it passed a block-size test that was genuinely wrong
// by 46%. `Approx(0.0).epsilon(0.0)` fails the opposite way: it is `0 < 0`, so a
// value of exactly zero does not match zero.
double relative_error(double measured, double expected) {
	const double scale = std::fabs(expected) > std::fabs(measured) ? std::fabs(expected)
																  : std::fabs(measured);
	if (scale == 0.0) {
		return 0.0;
	}
	return std::fabs(measured - expected) / scale;
}

// Goertzel magnitude at an arbitrary frequency, normalized by length.
//
// Used instead of the DFT-bin helper in `test_engine_synth.cpp` because these
// buffers are noise and there is no fundamental to make the length a whole number
// of periods of. Probing at chosen frequencies rather than at bin centers is the
// point: the question here is where the layer's energy sits, which does not land
// on a bin.
//
// **Only ever used for a centroid, never for a peak.** One Goertzel bin of one
// realization of filtered noise is a Rayleigh-distributed random variable, so the
// argmax over a sweep of them is mostly noise — that mistake is what the sine
// sweep below exists to avoid, and it cost a wrong verdict on the filter before it
// was caught.
double tone_magnitude(const std::vector<float> &samples, double freq_hz, double sample_rate) {
	const int count = static_cast<int>(samples.size());
	const double w = 2.0 * kart::core::PI * freq_hz / sample_rate;
	const double coeff = 2.0 * std::cos(w);
	double s1 = 0.0;
	double s2 = 0.0;
	for (int i = 0; i < count; ++i) {
		const double s0 = static_cast<double>(samples[i]) + coeff * s1 - s2;
		s2 = s1;
		s1 = s0;
	}
	const double real = s1 - s2 * std::cos(w);
	const double imag = s2 * std::sin(w);
	return std::sqrt(real * real + imag * imag) / static_cast<double>(count);
}

// The filter's frequency response, measured by driving it with a sine at each
// probe frequency and reading the settled output amplitude.
//
// Deterministic, and that is why it is a sweep of sines rather than one pass of
// noise: the response of a two-pole filter is a smooth curve, and estimating a
// smooth curve from a single noise realization gives a jagged one whose argmax is
// wherever the PRNG happened to be loud. Measured that way the band read 776 Hz
// with a realized Q of 10.2, against 900 Hz and 2.4 asked for; measured this way
// it reads what the coefficients say.
std::vector<double> svf_response(double cutoff, double q, const std::vector<double> &probes,
		bool bandpass, bool tilt = false) {
	std::vector<double> magnitudes;
	magnitudes.reserve(probes.size());
	const double tilt_alpha = 1.0 - std::exp(-2.0 * kart::core::PI * cutoff *
			tuning::SCRUB_TILT_LP_RATIO / SAMPLE_RATE);
	for (const double f : probes) {
		Svf filter;
		double tilt_state = 0.0;
		filter.set(cutoff, q, SAMPLE_RATE);
		// Long enough that a Q of a few has rung down its transient — 4,800 samples
		// is 100 ms — and then a whole number of cycles is not needed because the
		// amplitude is taken as a peak over the last stretch of a pure tone.
		const int settle = 4800;
		const int measure = 4800;
		double peak = 0.0;
		for (int i = 0; i < settle + measure; ++i) {
			const double phase = 2.0 * kart::core::PI * f * static_cast<double>(i) / SAMPLE_RATE;
			double lp = 0.0;
			double bp = 0.0;
			filter.process(std::sin(phase), lp, bp);
			double y = bandpass ? bp : lp;
			if (tilt) {
				tilt_state += (y - tilt_state) * tilt_alpha;
				y = tilt_state;
			}
			if (i >= settle) {
				peak = std::fabs(y) > peak ? std::fabs(y) : peak;
			}
		}
		magnitudes.push_back(peak);
	}
	return magnitudes;
}

// The largest absolute sample in a buffer.
//
// Used only alongside `rms`, never instead of it. A peak is one draw of a PRNG
// and says nothing about a level -- that is why every level assertion in this
// file is an RMS. It says everything about a *ceiling*, which is the one question
// an RMS cannot answer.
double peak(const std::vector<float> &samples) {
	double worst = 0.0;
	for (const float sample : samples) {
		const double magnitude = std::fabs(static_cast<double>(sample));
		worst = magnitude > worst ? magnitude : worst;
	}
	return worst;
}

// Invert `kart::core::soft_clip`, so a clipped peak can be reported as the sample
// that *would* have been written.
//
// This is what lets a test prove the clamp is load-bearing rather than
// decorative. Without it the only assertable property is "no sample exceeded
// 1.0", which a clamp satisfies analytically whatever it is protecting against --
// a test that would pass just as happily if the defect had never existed. Run
// backwards through the same knee, a measured output peak of 0.9982 is an input
// of 1.33, and *that* is the number that says the layer was clipping.
//
// `atanh` of anything at or past 1.0 is infinite; the clamp's output cannot reach
// 1.0 in exact arithmetic but can round to it in a float, so the argument is
// pulled just short and the result reported as a lower bound.
double unclipped(double clipped) {
	const double threshold = kart::core::synth_tuning::SOFT_CLIP_THRESHOLD;
	const double headroom = 1.0 - threshold;
	const double magnitude = std::fabs(clipped);
	if (magnitude <= threshold) {
		return magnitude;
	}
	double t = (magnitude - threshold) / headroom;
	if (t > 1.0 - 1e-12) {
		t = 1.0 - 1e-12;
	}
	return threshold + headroom * std::atanh(t);
}

EngineAudioInput sliding(double scrub, double speed_ms, int surface_type) {
	EngineAudioInput input;
	input.scrub = scrub;
	input.speed_ms = speed_ms;
	input.surface = surface_type;
	return input;
}

// Run a synth to steady state and report the RMS over a **fixed measurement
// window**, whatever block size it was rendered in.
//
// Two things this has to get right, and the first draft got neither:
//
// **Steady state.** The scrub layer's drive one-pole has a ~35 ms time constant,
// so a single block measures the ramp rather than the level. Two seconds is ~57
// time constants.
//
// **A window long enough to average.** The first version returned the RMS of the
// last block, which made the answer depend on the block size — not because the
// synth did, but because band-passed noise at Q 2.4 has a correlation time of
// about 2.7 ms and a 64-sample block is half of one. It read 0.0378 at 64 frames
// against 0.0258 at 2048 for identical state. MEASURE_FRAMES is 16,384, about 48
// correlation times, which brings the sampling error under 1%.
constexpr int MEASURE_FRAMES = 1 << 14;

template <typename Synth>
std::vector<float> settled_window(Synth &synth, const EngineAudioInput &input, int block = 512,
		double seconds = 2.0, int frames = MEASURE_FRAMES) {
	synth.reset();
	synth.publish(input);
	const int blocks = static_cast<int>(seconds * SAMPLE_RATE / block);
	std::vector<float> out(block);
	for (int b = 0; b < blocks; ++b) {
		synth.render(out.data(), block);
	}
	std::vector<float> window;
	window.reserve(static_cast<size_t>(frames));
	while (static_cast<int>(window.size()) < frames) {
		synth.render(out.data(), block);
		window.insert(window.end(), out.begin(), out.end());
	}
	window.resize(static_cast<size_t>(frames));
	return window;
}

template <typename Synth>
double settled_rms(Synth &synth, const EngineAudioInput &input, int block = 512,
		double seconds = 2.0) {
	return rms(settled_window(synth, input, block, seconds));
}

} // namespace

TEST_CASE("a stationary kart is silent however hard its tires are slipping") {
	// The one amplitude claim in this file that is derived rather than guessed.
	// Radiated power goes with dissipated power, dissipated power is force times
	// slip velocity, and slip velocity at a fixed slip angle is proportional to
	// road speed. A kart held on the brakes with the wheels turned makes no scrub
	// noise, and a law that says otherwise is wrong whatever its spectrum is.
	ScrubSynth synth;
	synth.configure(ScrubWindConfig(), SAMPLE_RATE);

	const double at_rest = settled_rms(synth, sliding(1.0, 0.0, kart::core::SURFACE_ASPHALT));
	CHECK(at_rest == 0.0);

	// And the wind layer, for the same reason from the other direction: no air
	// moving past a parked kart.
	WindSynth wind;
	wind.configure(ScrubWindConfig(), SAMPLE_RATE);
	const double still = settled_rms(wind, sliding(0.0, 0.0, kart::core::SURFACE_ASPHALT));
	CHECK(still == 0.0);

	MESSAGE("scrub at rest, full slip: " << at_rest << " RMS; wind at rest: " << still);
}

TEST_CASE("scrub level rises with slip and with speed, and saturates where it says") {
	ScrubSynth synth;
	synth.configure(ScrubWindConfig(), SAMPLE_RATE);

	const double speed = 30.0; // past SCRUB_FULL_SPEED_MS, so the ramp is saturated
	double previous = -1.0;
	std::string row;
	for (int step = 0; step <= 10; ++step) {
		const double drive = 0.1 * static_cast<double>(step);
		const double level = settled_rms(synth, sliding(drive, speed, kart::core::SURFACE_ASPHALT));
		CHECK(level > previous);
		previous = level;
		row += " " + std::to_string(level);
	}
	MESSAGE("scrub RMS against drive 0.0..1.0 at 30 m/s:" << row);

	// The speed ramp, at a fixed drive. Monotone up to the reference speed and
	// flat past it.
	const double half = settled_rms(synth,
			sliding(1.0, 0.5 * tuning::SCRUB_FULL_SPEED_MS, kart::core::SURFACE_ASPHALT));
	const double full = settled_rms(synth,
			sliding(1.0, tuning::SCRUB_FULL_SPEED_MS, kart::core::SURFACE_ASPHALT));
	const double past = settled_rms(synth,
			sliding(1.0, 3.0 * tuning::SCRUB_FULL_SPEED_MS, kart::core::SURFACE_ASPHALT));
	CHECK(half < full);
	CHECK(relative_error(past, full) < 1e-9);
	MESSAGE("scrub RMS at half / full / 3x the reference speed: "
			<< half << " " << full << " " << past);
}

TEST_CASE("the scrub band reproduces the width and the skirts that were measured") {
	// This is the test that ties the code to `docs/REFERENCES.md`, and it is a
	// different test from the one that was here before the sourcing pass. Then, the
	// constants were admitted guesses and all a test could hold was that the filter
	// landed where the knob said. Now `kz_audio::SCRUB_PEAK_HZ`, `SCRUB_WIDTH_OCT`
	// and the two slopes are measured numbers off three CC0 recordings, so the
	// question is whether the filter that ships actually reproduces them.
	//
	// **The cascade is the finding.** One two-pole band-pass asymptotes at
	// +/-6 dB/octave and the measurement is +9.7 and -14.0, so a single section
	// falls at less than half the measured rate whatever its Q is set to. Two
	// identical sections give +/-12 and bracket both. This test is what says so.
	const double center = kart::core::kz_audio::SCRUB_PEAK_HZ;
	const double q = tuning::band_q_for_width_oct(kart::core::kz_audio::SCRUB_WIDTH_OCT);

	std::vector<double> probes;
	for (double f = 60.0; f < 12000.0; f *= 1.02) {
		probes.push_back(f);
	}
	const std::vector<double> mags = svf_response(center, q, probes, true, true);

	int peak = 0;
	for (size_t i = 1; i < mags.size(); ++i) {
		if (mags[i] > mags[peak]) {
			peak = static_cast<int>(i);
		}
	}
	const double measured_center = probes[peak];
	MESSAGE("scrub band peak: asked " << center << " Hz, measured " << measured_center << " Hz");
	CHECK(relative_error(measured_center, center) < 0.01);

	// Interpolated between the bracketing probes rather than snapped to one. Taking
	// the first probe past the crossing biases the width wide every time; on a 5%
	// grid that was 15% of the answer, in the direction the bias predicts.
	auto crossing = [&](double target, int direction) {
		int i = peak;
		while (i > 0 && i + 1 < static_cast<int>(mags.size()) && mags[i] > target) {
			i += direction;
		}
		const int prev = i - direction;
		const double m0 = std::log(mags[prev]);
		const double m1 = std::log(mags[i]);
		const double f0 = std::log(probes[prev]);
		const double f1 = std::log(probes[i]);
		const double t = (std::log(target) - m0) / (m1 - m0);
		return std::exp(f0 + t * (f1 - f0));
	};

	// The -10 dB width, in octaves, against the number measured off the recordings.
	const double minus_10 = mags[peak] * 0.31622777;
	const double lower = crossing(minus_10, -1);
	const double upper = crossing(minus_10, +1);
	const double width_oct = std::log2(upper / lower);
	MESSAGE("-10 dB band " << lower << " .. " << upper << " Hz = " << width_oct
			<< " octaves, against " << kart::core::kz_audio::SCRUB_WIDTH_OCT << " measured");
	CHECK(relative_error(width_oct, kart::core::kz_audio::SCRUB_WIDTH_OCT) < 0.02);

	// The skirts, over the same spans the recordings were fitted over: the octave
	// below the peak and the two octaves above it.
	auto db_at = [&](double f) {
		const std::vector<double> one = svf_response(center, q, { f }, true, true);
		return 20.0 * std::log10(one[0] / mags[peak]);
	};

	const double below_slope = db_at(center / 2.0) - db_at(center / 4.0);
	const double above_slope = db_at(center * 4.0) - db_at(center * 2.0);
	MESSAGE("skirts: " << below_slope << " dB/oct below, " << above_slope
			<< " above; the recordings gave "
			<< kart::core::kz_audio::SCRUB_SLOPE_BELOW_DB_OCT << " and "
			<< kart::core::kz_audio::SCRUB_SLOPE_ABOVE_DB_OCT);

	// **The band is asymmetric and that is the point of the tilt.** A band-pass on
	// its own is symmetric by construction, so the two skirts would be equal and
	// the upper one would be 6 dB/octave short of the measurement. This asserts the
	// asymmetry exists and has the sign the recordings show.
	CHECK(-above_slope > below_slope + 3.0);

	// The upper skirt against the measurement. Within 1 dB/octave.
	CHECK(std::fabs(above_slope - kart::core::kz_audio::SCRUB_SLOPE_ABOVE_DB_OCT) < 1.0);

	// The lower skirt is knowingly about 2.5 dB/octave short of +9.7, and
	// `SCRUB_TILT_LP_RATIO`'s comment says why closing it would be over-fitting
	// three recordings of two passenger cars to a tire this project does not use.
	// Asserted as a bound rather than left unstated, so that a change which made it
	// worse would be caught.
	CHECK(below_slope > 6.5);
	CHECK(below_slope < kart::core::kz_audio::SCRUB_SLOPE_BELOW_DB_OCT);
}

TEST_CASE("the layer level does not move when the band width does") {
	// **The defect this asserts against shipped and was caught by driving.** The
	// Cytomic band-pass output has a peak gain of Q rather than 1 and its noise
	// bandwidth falls as 1/Q, so white noise through it comes out proportional to
	// sqrt(Q): measured 0.127 at Q 0.8 against 0.775 at Q 20, a 6:1 swing. That
	// made `scrub_gain` not a gain. A driver widening the band to taste would have
	// found it quieter and reached for the level knob, and the two would fight
	// every time.
	//
	// It is also why the layer was inaudible on the first drive: at the shipped Q
	// of 6.4 the chain passes 0.437 of white, so a `scrub_gain` of 0.30 was really
	// 0.13 against an engine voice at 0.18.
	//
	// #160's third acceptance item is that turning one thing must not require
	// re-judging another. This is that property inside one layer, and it is the
	// reason `SCRUB_CHAIN_RMS_C` exists.
	ScrubSynth synth;
	const EngineAudioInput state = sliding(1.0, 30.0, kart::core::SURFACE_ASPHALT);

	std::string row;
	double first = 0.0;
	bool first_set = false;
	double worst = 0.0;
	// The range the surface table actually produces is 1.9 on grass to 6.4 on
	// asphalt; the wider sweep is the adjustable range a driver can reach on F2.
	for (const double q : { 1.0, 1.9, 3.0, 6.402, 10.0 }) {
		ScrubWindConfig config;
		config.scrub_q = q;
		synth.configure(config, SAMPLE_RATE);
		const double level = settled_rms(synth, state);
		if (!first_set) {
			first = level;
			first_set = true;
		}
		worst = std::max(worst, relative_error(level, first));
		row += "  Q " + std::to_string(q) + " -> " + std::to_string(level);
	}
	MESSAGE("RMS against band Q:" << row);
	MESSAGE("worst deviation from the Q=1.0 level: " << (100.0 * worst) << "%");
	// Flat to within the fit's residual. Before the normalization this spanned 6:1.
	CHECK(worst < 0.05);

	// And the same for the band center, which the surface table also moves: grass
	// pulls the band from 1000 Hz down to 456 Hz, and running onto it must change
	// the character without changing the volume.
	double center_worst = 0.0;
	double center_first = 0.0;
	bool center_set = false;
	std::string center_row;
	for (const double hz : { 456.0, 700.0, 1000.0, 1600.0 }) {
		ScrubWindConfig config;
		config.scrub_center_hz = hz;
		synth.configure(config, SAMPLE_RATE);
		const double level = settled_rms(synth, state);
		if (!center_set) {
			center_first = level;
			center_set = true;
		}
		center_worst = std::max(center_worst, relative_error(level, center_first));
		center_row += "  " + std::to_string(hz) + " Hz -> " + std::to_string(level);
	}
	MESSAGE("RMS against band center:" << center_row);
	CHECK(center_worst < 0.10);
}

TEST_CASE("the shipped defaults are the arithmetic on the measurement, not a retype") {
	// `core/tuning.h`'s table has to hold literals — `Tunable` is `constexpr` and
	// `std::pow` is not usable in one — so the two derived audio defaults are
	// numbers written out by hand next to a citation explaining where they came
	// from. That is exactly the "second owner of a justification" this project
	// warns about, and the answer is to enforce the relationship rather than trust
	// the comment.
	const double q = tuning::band_q_for_width_oct(kart::core::kz_audio::SCRUB_WIDTH_OCT);
	MESSAGE("band_q_for_width_oct(" << kart::core::kz_audio::SCRUB_WIDTH_OCT << ") = " << q
			<< ", tuning.h carries " << kart::core::TUNABLES[kart::core::TUNE_SCRUB_Q].default_value);
	CHECK(relative_error(kart::core::TUNABLES[kart::core::TUNE_SCRUB_Q].default_value, q) < 0.001);

	const double n = tuning::wind_exponent_for_db_per_doubling(
			kart::core::kz_audio::WIND_DB_PER_SPEED_DOUBLING);
	MESSAGE(kart::core::kz_audio::WIND_DB_PER_SPEED_DOUBLING << " dB per doubling = exponent "
			<< n << ", tuning.h carries "
			<< kart::core::TUNABLES[kart::core::TUNE_WIND_SPEED_EXPONENT].default_value);
	CHECK(relative_error(kart::core::TUNABLES[kart::core::TUNE_WIND_SPEED_EXPONENT].default_value, n)
			< 0.005);

	// And the center, which is a plain copy rather than arithmetic — checked anyway,
	// because a plain copy is the easiest kind to let drift.
	CHECK(kart::core::TUNABLES[kart::core::TUNE_SCRUB_CENTER_HZ].default_value ==
			kart::core::kz_audio::SCRUB_PEAK_HZ);

	// The config a scene gets with no tuning at all must agree with the table, or
	// the overlay would open showing a value the synth is not using.
	const ScrubWindConfig defaults;
	CHECK(relative_error(defaults.scrub_q, q) < 1e-12);
	CHECK(relative_error(defaults.wind_speed_exponent, n) < 1e-12);
	CHECK(defaults.scrub_center_hz == kart::core::kz_audio::SCRUB_PEAK_HZ);

	// **The gains too, because one of them drifted the first time it was moved.**
	// Raising `scrub_gain` after the first drive touched `core/tuning.h` and
	// `engine_voice_rig.gd` and left `ScrubWindConfig` behind, so a stream nobody
	// tuned would have seeded itself from the old value while the overlay showed
	// the new one. Three owners of one number is exactly what these checks are for.
	CHECK(defaults.scrub_gain ==
			kart::core::TUNABLES[kart::core::TUNE_SCRUB_GAIN].default_value);
	CHECK(defaults.wind_gain ==
			kart::core::TUNABLES[kart::core::TUNE_WIND_GAIN].default_value);
	CHECK(defaults.scrub_gamma ==
			kart::core::TUNABLES[kart::core::TUNE_SCRUB_GAMMA].default_value);
	CHECK(defaults.scrub_full_speed_ms ==
			kart::core::TUNABLES[kart::core::TUNE_SCRUB_FULL_SPEED].default_value);
	CHECK(defaults.wind_cutoff_hz_per_ms ==
			kart::core::TUNABLES[kart::core::TUNE_WIND_CUTOFF_PER_MS].default_value);
}

TEST_CASE("no operating point writes a sample outside full scale") {
	// **The defect this asserts against shipped and it was not subtle.**
	// `ScrubSynth::render` wrote `tilt_ * level_` straight to the buffer with no
	// clamp of any kind, while `EngineSynth` has had `soft_clip` since it was
	// written. At the shipped `scrub_gain` of 0.45, drive 1.0 and any road speed at
	// or above `SCRUB_FULL_SPEED_MS` the peak sample was **1.2546** on asphalt and
	// **1.3422** on dirt, measured over 2^20 frames. Past full scale is not a level
	// problem, it is whatever the device decides to do with an out-of-range float.
	//
	// **Asserting only "no sample exceeds 1.0" would be a test that cannot fail.**
	// A clamp guarantees that analytically, so the assertion would pass identically
	// on a build where the defect never existed and on one where the layer is
	// slamming into the limiter on every corner. So this runs the measured peak
	// back through the inverse of the knee -- `unclipped` above -- and asserts on
	// the sample that *would* have been written. That number is the defect.
	const double threshold = kart::core::synth_tuning::SOFT_CLIP_THRESHOLD;

	// The whole operating box. Drive is the 0..1 slip mean, speed spans a
	// standstill to well past the ramp's saturation, and every surface, because the
	// surface moves both the band center and its Q and therefore the crest factor
	// of what comes out. 12 m/s is in the list on purpose: it is exactly where the
	// speed ramp saturates and where the pre-clamp peak first went past 1.0.
	double worst_out = 0.0;
	double worst_in = 0.0;
	std::string worst_where;
	for (int surface_type = 0; surface_type < kart::core::SURFACE_COUNT; ++surface_type) {
		for (const double drive : { 0.0, 0.35, 0.7, 1.0 }) {
			for (const double speed : { 0.0, 4.0, 12.0, 45.0 }) {
				ScrubSynth synth;
				synth.configure(ScrubWindConfig(), SAMPLE_RATE);
				// 0.5 s of settle is fourteen of the drive one-pole's ~35 ms time
				// constants, and 2^17 frames is 2.7 s of measurement -- enough that
				// the crest factor has converged to within a few percent without
				// making a 64-point sweep the slowest thing in the suite.
				const std::vector<float> window = settled_window(synth,
						sliding(drive, speed, surface_type), 512, 0.5, 1 << 17);
				const double p = peak(window);
				// Full scale is a closed interval: a sample AT 1.0 is representable
				// and fine, a sample past it is not.
				CHECK(p <= 1.0);
				if (p > worst_out) {
					worst_out = p;
					worst_in = unclipped(p);
					worst_where = kart::core::surface(surface_type).name + std::string(" drive ") +
							std::to_string(drive) + " at " + std::to_string(speed) + " m/s";
				}
			}
		}
	}
	MESSAGE("worst scrub peak over the operating box: " << worst_out << " out, " << worst_in
			<< " before the knee, at " << worst_where);

	// **At the shipped gain the knee is now idle, and that is the fix and not a
	// regression.** This assertion ran the other way when it was written, against
	// `scrub_gain = 0.45`: the layer peaked at 1.2546 on asphalt and the clamp was
	// carrying it. #160 then measured that same 0.45 sitting 16.3-16.7 dB above the
	// engine note and dropped it to 0.035, which is 22 dB of margin and puts the
	// peak nowhere near the threshold.
	//
	// So the clamp is a safety net again rather than a limiter, which is what a
	// clamp should be. What proves it is still wired up is the F2-range case below,
	// where the gain is turned to the top of its own tunable range and the output
	// still cannot leave full scale. ADR-0039.
	ScrubSynth shipped;
	shipped.configure(ScrubWindConfig(), SAMPLE_RATE);
	const std::vector<float> loud = settled_window(shipped,
			sliding(1.0, 30.0, kart::core::SURFACE_ASPHALT), 512, 2.0, 1 << 20);
	const double shipped_peak = peak(loud);
	const double shipped_input = unclipped(shipped_peak);
	MESSAGE("scrub at the shipped defaults, drive 1.0, 30 m/s: peak " << shipped_peak
			<< " out of " << shipped_input << " in, against a knee at " << threshold);
	CHECK(shipped_peak < 1.0);
	CHECK(shipped_input < threshold);

	// And the wind layer, which got the same clamp for a different reason: it was
	// under full scale before, but normalizing its chain raised it by 13.0 dB at
	// the reference speed and `wind_gain` runs to 1.0 on F2. At the shipped 0.12 it
	// stays well clear of the knee, which is what a safety net should look like.
	WindSynth wind;
	wind.configure(ScrubWindConfig(), SAMPLE_RATE);
	const std::vector<float> gale = settled_window(wind,
			sliding(0.0, 38.0, kart::core::SURFACE_ASPHALT), 512, 0.5, 1 << 20);
	MESSAGE("wind at the shipped gain and the reference speed: peak " << peak(gale)
			<< ", RMS " << rms(gale));
	CHECK(peak(gale) <= 1.0);
	CHECK(peak(gale) < threshold);

	// The top of both F2 ranges, where the clamp stops being a net and becomes a
	// limiter. `core/tuning.h` caps `scrub_gain` at 2.00 and `wind_gain` at 1.00;
	// the literals are here rather than read from `TUNABLES` because what is being
	// asserted is that the ceiling holds at an absurd gain, not that a particular
	// row still says 2.00.
	//
	// **Both report exactly 1.0 here and that is correct, not a failure.**
	// `soft_clip` is asymptotic to 1.0 in double and cannot reach it, but the cast
	// to `float` has 24 bits of mantissa, so anything past about 1 - 3e-8 rounds to
	// 1.0f. That value is representable and inside full scale. It is why every
	// assertion in this test is `<= 1.0` and not `< 1.0`: the property being
	// defended is "no sample outside full scale", and 1.0 is not outside it.
	ScrubWindConfig hot;
	hot.scrub_gain = 2.0;
	hot.wind_gain = 1.0;
	ScrubSynth hot_scrub;
	hot_scrub.configure(hot, SAMPLE_RATE);
	const double hot_scrub_peak = peak(settled_window(hot_scrub,
			sliding(1.0, 45.0, kart::core::SURFACE_DIRT), 512, 0.5, 1 << 18));
	WindSynth hot_wind;
	hot_wind.configure(hot, SAMPLE_RATE);
	const double hot_wind_peak = peak(settled_window(hot_wind,
			sliding(0.0, 45.0, kart::core::SURFACE_ASPHALT), 512, 0.5, 1 << 18));
	MESSAGE("at the top of both F2 gain ranges: scrub peak " << hot_scrub_peak << ", wind peak "
			<< hot_wind_peak);
	CHECK(hot_scrub_peak <= 1.0);
	CHECK(hot_wind_peak <= 1.0);
}

TEST_CASE("wind_gain is a level and the speed law is the only thing that scales it") {
	// **The same defect `SCRUB_CHAIN_RMS_C` fixed for the scrub band, one layer
	// over, unfixed for a commit longer.**
	//
	// The wind low-pass's corner is `WIND_CUTOFF_FLOOR_HZ + wind_cutoff_hz_per_ms *
	// speed`, so it rises with road speed, and a low-pass passes more noise power
	// the higher its corner sits. That gain was never divided out, so it rode on
	// top of the published speed law. Measured through the shipped chain before the
	// fix, over 2^21 frames per point:
	//
	//     4.75 -> 9.5 m/s   20.00 dB per doubling
	//     9.5  -> 19        20.41
	//     19   -> 38        20.66
	//
	// against `kz_audio::WIND_DB_PER_SPEED_DOUBLING` = 18.0, climbing toward the
	// asymptote 18.06 + 3.01 = 21.07 as the 90 Hz floor stops mattering. Between
	// 2.0 and 2.7 dB per doubling was arriving from a filter rather than from Brown
	// and Gordon and Lower et al., and `wind_speed_exponent` -- a `Derived` row
	// whose whole citation is that number -- was therefore describing something the
	// layer did not do.
	const double sourced = kart::core::kz_audio::WIND_DB_PER_SPEED_DOUBLING;

	// Four speeds spanning a factor of eight, so three independent doublings.
	// 2^21 frames is 43.7 s per point. The RMS of band-limited noise over T seconds
	// has a relative error of about 0.5/sqrt(2*ENBW*T); the narrowest corner here
	// is 213 Hz, giving ENBW 237 Hz and 20,700 independent samples, so 0.35% or
	// 0.030 dB. A doubling is a difference of two of those, so the statistical
	// floor on each figure below is about 0.04 dB and 0.15 dB is a bit under four
	// sigma. That is two orders inside the 2.49 dB defect and still tight enough to
	// catch a tenth of it.
	constexpr int LONG_WINDOW = 1 << 21;
	constexpr double TOLERANCE_DB = 0.15;
	const std::vector<double> speeds = { 4.75, 9.5, 19.0, 38.0 };

	std::vector<double> levels;
	std::string table;
	for (const double speed : speeds) {
		WindSynth synth;
		synth.configure(ScrubWindConfig(), SAMPLE_RATE);
		const std::vector<float> window = settled_window(synth,
				sliding(0.0, speed, kart::core::SURFACE_ASPHALT), 512, 0.5, LONG_WINDOW);
		levels.push_back(rms(window));
		// If the knee were engaging, this would be a measurement of the limiter and
		// not of the speed law. It is not: 0.297 against a knee at 0.8.
		CHECK(peak(window) < kart::core::synth_tuning::SOFT_CLIP_THRESHOLD);
		table += "\n  " + std::to_string(speed) + " m/s  RMS " + std::to_string(levels.back());
	}
	MESSAGE("wind level against speed:" << table);

	double worst = 0.0;
	std::string doublings;
	for (size_t i = 1; i < speeds.size(); ++i) {
		const double db = 20.0 * std::log10(levels[i] / levels[i - 1]) /
				std::log2(speeds[i] / speeds[i - 1]);
		doublings += "\n  " + std::to_string(speeds[i - 1]) + " -> " + std::to_string(speeds[i]) +
				" m/s: " + std::to_string(db) + " dB/doubling";
		worst = std::max(worst, std::fabs(db - sourced));
		CHECK(std::fabs(db - sourced) < TOLERANCE_DB);
	}
	MESSAGE("realized dB per doubling of road speed, against the sourced " << sourced
			<< ":" << doublings);
	MESSAGE("worst deviation from the sourced figure: " << worst << " dB");

	// **`wind_gain` is now a gain in the same units `scrub_gain` is**, and that --
	// not the absolute number -- is the property #160 needs to set the two against
	// each other at all. Both constants normalize by a filter's RMS *gain* and
	// neither divides out the noise source's own RMS of 1/sqrt(3), so both layers
	// realize about 0.577 of the number written on the knob. `scrub_wind.h`'s
	// header says why that offset is still there: closing it moves both shipped
	// levels by 4.77 dB and both gains have just been set by ear.
	//
	// So what is asserted is the cross-layer equality, measured at the operating
	// point each layer is loudest at. If these ever diverge, a mixing pass done on
	// one layer stops transferring to the other, which is the failure #160 cannot
	// absorb.
	const double at_reference = levels.back();
	const double wind_ratio = at_reference / ScrubWindConfig().wind_gain;

	ScrubSynth scrub;
	scrub.configure(ScrubWindConfig(), SAMPLE_RATE);
	// Gain reduced from the shipped 0.45 so the measurement is of the gain law and
	// not of the soft clip, which 0.45 reaches into. The ratio is the same either
	// side of that; see the full-scale test for the peak that says so.
	//
	// **`LONG_WINDOW` and not the file's default `MEASURE_FRAMES`.** The two layers
	// draw from different PCG32 streams, so unlike every other comparison in this
	// file their sampling errors do not cancel. Over 16,384 frames the scrub band
	// at Q 5.0 has about 180 correlation times in the window, which is a 3.7%
	// standard error -- and it read 0.630 against a converged 0.588, a two-sigma
	// excursion that failed a 5% check on the first run. 2^21 frames brings it
	// under 0.4%.
	scrub.set_gain(0.1);
	const double scrub_ratio = rms(settled_window(scrub,
			sliding(1.0, 30.0, kart::core::SURFACE_ASPHALT), 512, 2.0, LONG_WINDOW)) / 0.1;

	MESSAGE("RMS per unit gain: wind " << wind_ratio << " at " << tuning::WIND_REFERENCE_SPEED_MS
			<< " m/s, scrub " << scrub_ratio << " at full drive and 30 m/s; the source's own RMS "
			<< "is 1/sqrt(3) = 0.5774");
	CHECK(relative_error(wind_ratio, scrub_ratio) < 0.05);
	CHECK(relative_error(wind_ratio, 1.0 / std::sqrt(3.0)) < 0.02);

	// **And the timbre knob no longer moves the level.** `wind_cutoff_hz_per_ms`
	// runs 2 to 120 on F2. Before the fix, winding it across that range moved the
	// layer's RMS at 38 m/s by a factor of 5.208 -- **14.33 dB** -- with `wind_gain`
	// untouched, which is issue #160's third acceptance item broken inside one
	// layer in exactly the way `scrub_q` broke it before `193d507`.
	//
	// The residual is the bilinear map: at the top of the range the corner is
	// 4,650 Hz, where the digital filter passes 1.4% less than the analog prototype
	// `wind_chain_rms` is derived from, so the last doubling reads about 0.09 dB
	// low. That is a known systematic and the tolerance is widened to cover it and
	// nothing else.
	double level_worst = 0.0;
	double level_first = 0.0;
	std::string knob_row;
	for (const double per_ms : { 2.0, 26.0, 60.0, 120.0 }) {
		ScrubWindConfig config;
		config.wind_cutoff_hz_per_ms = per_ms;

		std::vector<double> knob_levels;
		for (const double speed : speeds) {
			WindSynth synth;
			synth.configure(config, SAMPLE_RATE);
			knob_levels.push_back(rms(settled_window(synth,
					sliding(0.0, speed, kart::core::SURFACE_ASPHALT), 512, 0.5, LONG_WINDOW)));
		}
		for (size_t i = 1; i < speeds.size(); ++i) {
			const double db = 20.0 * std::log10(knob_levels[i] / knob_levels[i - 1]) /
					std::log2(speeds[i] / speeds[i - 1]);
			CHECK(std::fabs(db - sourced) < 0.25);
		}

		if (level_first == 0.0) {
			level_first = knob_levels.back();
		}
		level_worst = std::max(level_worst, relative_error(knob_levels.back(), level_first));
		knob_row += "\n  " + std::to_string(per_ms) + " Hz s/m -> RMS at 38 m/s " +
				std::to_string(knob_levels.back());
	}
	MESSAGE("level against the wind cutoff knob:" << knob_row);
	MESSAGE("worst level deviation across the knob's whole range: " << (100.0 * level_worst)
			<< "%, where before the fix the same range moved the level by a factor of 5.208");
	CHECK(level_worst < 0.03);
}

TEST_CASE("a rough surface moves the scrub band down and widens it") {
	// `surface.h`'s `roughness` column says in its own comment that it exists for
	// this layer. Grass and dirt are within 6% of each other on grip and that file
	// says they are told apart by sound — this is the parameter that tells them
	// apart, so it has to actually move.
	ScrubSynth synth;
	synth.configure(ScrubWindConfig(), SAMPLE_RATE);

	std::vector<float> asphalt(1 << 15);
	synth.reset();
	synth.publish(sliding(1.0, 30.0, kart::core::SURFACE_ASPHALT));
	for (int b = 0; b < 8; ++b) {
		synth.render(asphalt.data(), static_cast<int>(asphalt.size()));
	}

	std::vector<float> grass(1 << 15);
	synth.reset();
	synth.publish(sliding(1.0, 30.0, kart::core::SURFACE_GRASS));
	for (int b = 0; b < 8; ++b) {
		synth.render(grass.data(), static_cast<int>(grass.size()));
	}

	// Spectral centroid, over a coarse log sweep. A centroid rather than a peak
	// because a widened band has a much less well-defined peak, which is the whole
	// point of widening it.
	auto centroid = [](const std::vector<float> &buffer) {
		double weighted = 0.0;
		double total = 0.0;
		for (double f = 80.0; f < 8000.0; f *= 1.10) {
			const double m = tone_magnitude(buffer, f, SAMPLE_RATE);
			weighted += f * m;
			total += m;
		}
		return total > 0.0 ? weighted / total : 0.0;
	};

	const double asphalt_centroid = centroid(asphalt);
	const double grass_centroid = centroid(grass);
	MESSAGE("scrub centroid: asphalt " << asphalt_centroid << " Hz, grass "
			<< grass_centroid << " Hz");
	CHECK(grass_centroid < asphalt_centroid);
}

TEST_CASE("the wind corner moves with speed, and the level is monotone in it") {
	// The Strouhal proportionality: f = St*U/L, so a fixed geometry's noise gets
	// brighter with speed rather than only louder. This is the second derived claim
	// in the file and the only one that can be checked without an ear.
	WindSynth synth;
	synth.configure(ScrubWindConfig(), SAMPLE_RATE);

	struct Row {
		double speed;
		double level;
		double centroid;
	};
	std::vector<Row> rows;

	for (const double speed : { 5.0, 10.0, 20.0, 30.0, 38.0 }) {
		std::vector<float> out(1 << 15);
		synth.reset();
		synth.publish(sliding(0.0, speed, kart::core::SURFACE_ASPHALT));
		for (int b = 0; b < 8; ++b) {
			synth.render(out.data(), static_cast<int>(out.size()));
		}
		double weighted = 0.0;
		double total = 0.0;
		for (double f = 30.0; f < 8000.0; f *= 1.10) {
			const double m = tone_magnitude(out, f, SAMPLE_RATE);
			weighted += f * m;
			total += m;
		}
		rows.push_back({ speed, rms(out), total > 0.0 ? weighted / total : 0.0 });
	}

	std::string table;
	for (const Row &row : rows) {
		table += "\n  " + std::to_string(row.speed) + " m/s  RMS " +
				std::to_string(row.level) + "  centroid " + std::to_string(row.centroid) + " Hz";
	}
	MESSAGE("wind against speed:" << table);

	for (size_t i = 1; i < rows.size(); ++i) {
		CHECK(rows[i].level > rows[i - 1].level);
		CHECK(rows[i].centroid > rows[i - 1].centroid);
	}
}

TEST_CASE("neither layer depends on the block size the device chose") {
	// `audio_state.h`: the device picks the block and the solver picks the tick,
	// and neither drives the other. A layer whose level depends on how many frames
	// it was handed is a layer that sounds different on a different machine.
	//
	// The two are rendered to the same total length in blocks of 64, 512 and 2048
	// from a common state, and the RMS is compared. The *samples* differ — the
	// filter's coefficient update lands at different points — but the level must
	// not.
	ScrubSynth scrub;
	WindSynth wind;
	const EngineAudioInput state = sliding(0.7, 25.0, kart::core::SURFACE_ASPHALT);

	std::string row;
	double first_scrub = 0.0;
	double first_wind = 0.0;
	int index = 0;
	for (const int block : { 64, 512, 2048 }) {
		scrub.configure(ScrubWindConfig(), SAMPLE_RATE);
		wind.configure(ScrubWindConfig(), SAMPLE_RATE);
		const double s = settled_rms(scrub, state, block);
		const double w = settled_rms(wind, state, block);
		if (index == 0) {
			first_scrub = s;
			first_wind = w;
		} else {
			// Noise RMS over a 2048-sample window has a sampling error of order
			// 1/sqrt(2N); 6% is comfortably outside that and far inside any real
			// level difference.
			CHECK(relative_error(s, first_scrub) < 0.03);
			CHECK(relative_error(w, first_wind) < 0.03);
		}
		row += "  " + std::to_string(block) + ": scrub " + std::to_string(s) +
				" wind " + std::to_string(w);
		++index;
	}
	MESSAGE("RMS by block size:" << row);
}

TEST_CASE("no discontinuity at a block boundary") {
	// The click test. Two adjacent blocks are rendered from one state and the
	// largest sample-to-sample step across the seam is compared against the largest
	// step inside the blocks. A coefficient update or a gain step at the seam shows
	// up here and nowhere else.
	ScrubSynth synth;
	synth.configure(ScrubWindConfig(), SAMPLE_RATE);
	synth.publish(sliding(0.8, 25.0, kart::core::SURFACE_ASPHALT));

	constexpr int BLOCK = 512;
	std::vector<float> joined;
	std::vector<float> block(BLOCK);
	for (int b = 0; b < 400; ++b) {
		synth.render(block.data(), BLOCK);
		if (b >= 380) { // settled
			joined.insert(joined.end(), block.begin(), block.end());
		}
	}

	double worst_seam = 0.0;
	double worst_interior = 0.0;
	for (size_t i = 1; i < joined.size(); ++i) {
		const double step = std::fabs(static_cast<double>(joined[i]) -
				static_cast<double>(joined[i - 1]));
		if (i % BLOCK == 0) {
			worst_seam = step > worst_seam ? step : worst_seam;
		} else {
			worst_interior = step > worst_interior ? step : worst_interior;
		}
	}
	MESSAGE("worst step at a seam " << worst_seam << " against " << worst_interior
			<< " inside a block");
	CHECK(worst_seam <= worst_interior);
}

TEST_CASE("two runs of the same input give the same bits") {
	// Same property the rest of `src/core/` is held to, and the reason both layers
	// carry their own explicitly seeded PCG32 stream: ARCHITECTURE.md §8 rule 3, so
	// what you hear does not depend on how many other things drew from a shared
	// generator.
	const EngineAudioInput state = sliding(0.6, 22.0, kart::core::SURFACE_CURB);

	auto capture = [&state](bool is_scrub) {
		std::vector<float> out(4096);
		if (is_scrub) {
			ScrubSynth synth;
			synth.configure(ScrubWindConfig(), SAMPLE_RATE);
			synth.publish(state);
			for (int b = 0; b < 8; ++b) {
				synth.render(out.data(), static_cast<int>(out.size()));
			}
		} else {
			WindSynth synth;
			synth.configure(ScrubWindConfig(), SAMPLE_RATE);
			synth.publish(state);
			for (int b = 0; b < 8; ++b) {
				synth.render(out.data(), static_cast<int>(out.size()));
			}
		}
		return out;
	};

	CHECK(capture(true) == capture(true));
	CHECK(capture(false) == capture(false));

	// And the two layers must not be the same noise: correlated streams would read
	// as one source being filtered two ways rather than as two sources.
	const std::vector<float> scrub_samples = capture(true);
	const std::vector<float> wind_samples = capture(false);
	CHECK(scrub_samples != wind_samples);
}

TEST_CASE("the layer gains are the only thing a scene may move") {
	// Issue #160 owns relative levels and is blocked on this file existing. What
	// this asserts is that the hook it needs is here and is linear, so a mixing
	// pass can reason about it: doubling the gain doubles the RMS exactly, with no
	// hidden shaping in between.
	//
	// **Below the knee, and the qualifier is not a weakening.** `soft_clip` is the
	// output ceiling and it is a nonlinearity by construction, so exact
	// proportionality is a property of the region under it and nowhere else. This
	// test used 0.2 and 0.4, and at drive 1.0 and 30 m/s a gain of 0.4 puts the
	// peak at 0.89 -- past the 0.8 knee, so it was measuring a mixture of the gain
	// law and the limiter and would have drifted as either moved. 0.1 and 0.2 put
	// the peak at 0.28 and 0.56, comfortably clear, and the peak is asserted rather
	// than assumed so this cannot go stale if the crest factor moves.
	ScrubSynth synth;
	synth.configure(ScrubWindConfig(), SAMPLE_RATE);
	const EngineAudioInput state = sliding(1.0, 30.0, kart::core::SURFACE_ASPHALT);

	synth.set_gain(0.1);
	const std::vector<float> quiet_window = settled_window(synth, state);
	const double quiet = rms(quiet_window);
	synth.set_gain(0.2);
	const std::vector<float> loud_window = settled_window(synth, state);
	const double loud = rms(loud_window);
	MESSAGE("scrub RMS at gain 0.1 / 0.2: " << quiet << " " << loud
			<< " (ratio " << (loud / quiet) << "), peaks " << peak(quiet_window) << " / "
			<< peak(loud_window));
	CHECK(peak(loud_window) < kart::core::synth_tuning::SOFT_CLIP_THRESHOLD);
	CHECK(relative_error(loud / quiet, 2.0) < 0.001);

	// And above the knee it is deliberately *not* linear, which is the whole point
	// of having one. Doubling 0.45 to 0.9 gives less than 6 dB, because 0.45
	// already reaches into the clamp -- see the full-scale test for the measured
	// 1.2546 that used to be written instead. Asserted as a bound rather than a
	// figure: what matters is that the ceiling binds, not by exactly how much.
	synth.set_gain(0.45);
	const double shipped = settled_rms(synth, state);
	synth.set_gain(0.9);
	const double doubled = settled_rms(synth, state);
	MESSAGE("scrub RMS at gain 0.45 / 0.9: " << shipped << " " << doubled << " (ratio "
			<< (doubled / shipped) << "); the clamp is what makes this less than 2");
	CHECK(doubled < 2.0 * shipped);
	CHECK(doubled > 1.5 * shipped);

	// A negative gain is silence rather than an inverted layer.
	synth.set_gain(-1.0);
	CHECK(settled_rms(synth, state) == 0.0);
}

TEST_CASE("the sourcing state is what every citation in this layer assumes") {
	// The tripwire, rewritten now that the sourcing pass has landed. It used to
	// assert both flags were false; the scrub flag is now true and the wind flag is
	// still false with a separate `WIND_SPECTRUM_SOURCED` beside it, because this
	// repository measured a scrub band and did not measure any wind.
	//
	// What this guards is the pair of caveats every citation in `core/tuning.h`
	// rests on. `scrub_center_hz` and `scrub_q` are classified `Derived` rather than
	// `Measured` **only** because the recordings are passenger-car radials. If
	// somebody measures a kart slick, those rows become `Measured`, become defended,
	// and the citations stop being true — and this is the check that says so.
	//
	// A `CHECK` and not a `static_assert`: this file has to keep compiling while
	// somebody is in the middle of doing that work.
	CHECK_MESSAGE(kart::core::kz_audio::SCRUB_SPECTRUM_MEASURED,
			"the scrub spectrum flag went back to false: scrub_wind.h derives its "
			"band from SCRUB_PEAK_HZ and SCRUB_WIDTH_OCT and core/tuning.h cites "
			"the recordings, so both would now be citing nothing");
	CHECK_MESSAGE(!kart::core::kz_audio::SCRUB_MEASURED_ON_KART_TIRE,
			"a kart tire was measured: core/tuning.h classifies scrub_center_hz and "
			"scrub_q as Derived specifically because the corpus is passenger-car "
			"radials. They should be Measured now, which also makes them defended");
	CHECK_MESSAGE(!kart::core::kz_audio::WIND_SPECTRUM_MEASURED,
			"a wind spectrum was measured here: WIND_DB_PER_SPEED_DOUBLING and the "
			"slope are currently transcribed from two published motorcycle papers, "
			"and their citations say so");
	CHECK_MESSAGE(kart::core::kz_audio::WIND_SPECTRUM_SOURCED,
			"the wind sourcing was withdrawn: wind_speed_exponent's 3.0 is 18 dB per "
			"doubling from Brown and Gordon and Lower et al., and without them it is "
			"the bare dipole guess scrub_wind.h originally refused to make");
}
