#include "doctest.h"

#include "core/audio_state.h"
#include "core/scrub_wind.h"
#include "core/surface.h"

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
		bool bandpass) {
	std::vector<double> magnitudes;
	magnitudes.reserve(probes.size());
	for (const double f : probes) {
		Svf filter;
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
			const double y = bandpass ? bp : lp;
			if (i >= settle) {
				peak = std::fabs(y) > peak ? std::fabs(y) : peak;
			}
		}
		magnitudes.push_back(peak);
	}
	return magnitudes;
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
double settled_rms(Synth &synth, const EngineAudioInput &input, int block = 512,
		double seconds = 2.0) {
	synth.reset();
	synth.publish(input);
	const int blocks = static_cast<int>(seconds * SAMPLE_RATE / block);
	std::vector<float> out(block);
	for (int b = 0; b < blocks; ++b) {
		synth.render(out.data(), block);
	}
	std::vector<float> window;
	window.reserve(MEASURE_FRAMES);
	while (static_cast<int>(window.size()) < MEASURE_FRAMES) {
		synth.render(out.data(), block);
		window.insert(window.end(), out.begin(), out.end());
	}
	window.resize(MEASURE_FRAMES);
	return rms(window);
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

TEST_CASE("the scrub band is where the coefficients were asked to put it") {
	// This is the test that separates "the constant is wrong" from "the filter is
	// wrong", and the distinction matters more here than anywhere else in the
	// audio path: every spectral number in `scrub_wind.h` is an admitted guess, so
	// the only thing a test can hold is that turning the knob moves the sound to
	// where the knob says.
	const double center = tuning::SCRUB_CENTER_HZ;
	std::vector<double> probes;
	for (double f = 100.0; f < 6000.0; f *= 1.05) {
		probes.push_back(f);
	}
	const std::vector<double> mags = svf_response(center, tuning::SCRUB_Q, probes, true);

	int peak = 0;
	for (size_t i = 1; i < mags.size(); ++i) {
		if (mags[i] > mags[peak]) {
			peak = static_cast<int>(i);
		}
	}
	const double measured_center = probes[peak];
	MESSAGE("scrub band asked for " << center << " Hz, peak measured at "
			<< measured_center << " Hz");
	// One probe step is 5%, so 12% is a little over two steps of tolerance.
	CHECK(relative_error(measured_center, center) < 0.01);

	// -3 dB points, and hence the realized Q. A band-pass at Q has its half-power
	// bandwidth at f0/Q, so this asserts the resonance knob does what it claims.
	//
	// **Interpolated between the bracketing probes rather than snapped to one.**
	// Taking the first probe at or below half power steps *past* the crossing
	// every time, so the bandwidth comes out systematically wide and the Q
	// systematically low — measured 2.03 against 2.4 that way, which reads exactly
	// like a filter defect and is not one. The grid is 5% per step and the error
	// was 15%, in the direction the bias predicts.
	const double half_power = mags[peak] * 0.70794578; // -3 dB
	auto crossing = [&](int from, int direction) {
		int i = from;
		while (i > 0 && i + 1 < static_cast<int>(mags.size()) && mags[i] > half_power) {
			i += direction;
		}
		// `i` is the first probe below half power and `i - direction` the last one
		// above it. Linear in log f against log magnitude, which is the natural
		// space for a filter skirt.
		const int prev = i - direction;
		const double m0 = std::log(mags[prev]);
		const double m1 = std::log(mags[i]);
		const double f0 = std::log(probes[prev]);
		const double f1 = std::log(probes[i]);
		const double t = (std::log(half_power) - m0) / (m1 - m0);
		return std::exp(f0 + t * (f1 - f0));
	};
	const double lower = crossing(peak, -1);
	const double upper = crossing(peak, +1);
	const double realized_q = measured_center / (upper - lower);
	MESSAGE("-3 dB points " << lower << " .. " << upper << " Hz, realized Q "
			<< realized_q << " against " << tuning::SCRUB_Q << " asked for");
	CHECK(relative_error(realized_q, tuning::SCRUB_Q) < 0.02);
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
	ScrubSynth synth;
	synth.configure(ScrubWindConfig(), SAMPLE_RATE);
	const EngineAudioInput state = sliding(1.0, 30.0, kart::core::SURFACE_ASPHALT);

	synth.set_gain(0.2);
	const double quiet = settled_rms(synth, state);
	synth.set_gain(0.4);
	const double loud = settled_rms(synth, state);
	MESSAGE("scrub RMS at gain 0.2 / 0.4: " << quiet << " " << loud
			<< " (ratio " << (loud / quiet) << ")");
	CHECK(relative_error(loud / quiet, 2.0) < 0.001);

	// A negative gain is silence rather than an inverted layer.
	synth.set_gain(-1.0);
	CHECK(settled_rms(synth, state) == 0.0);
}

TEST_CASE("the sourcing flags are still false, and this file is why that is fine") {
	// The tripwire. `engine_synth.h` carries the mirror image of this assert and
	// says why: if somebody measures a spectrum, the constants in `scrub_wind.h`
	// stop being guesses and every `Provenance::Unsourced` row in `core/tuning.h`
	// that cites this file becomes wrong.
	//
	// A static_assert would be the stronger form and is deliberately not used here:
	// this file must keep compiling while somebody is in the middle of doing that
	// work. A failing CHECK with this message is the right amount of friction.
	CHECK_MESSAGE(!kart::core::kz_audio::SCRUB_SPECTRUM_MEASURED,
			"a scrub spectrum was measured: scrub_wind_tuning's SCRUB_* constants and "
			"their core/tuning.h rows are still marked Unsourced and now lie");
	CHECK_MESSAGE(!kart::core::kz_audio::WIND_SPECTRUM_MEASURED,
			"a wind spectrum was measured: scrub_wind_tuning's WIND_* constants and "
			"their core/tuning.h rows are still marked Unsourced and now lie");
}
