#include "doctest.h"

#include "core/engine.h"
#include "core/engine_synth.h"
#include "core/kz_audio_reference.h"

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

// The engine note cannot be judged by a unit test the way a torque curve can, so
// what is asserted here is everything about it that *is* checkable in pure C++
// with no engine and no audio device: that the spectrum the synth actually
// produces is the ladder `kz_audio_reference.h` measured, that its fitted decay is
// nowhere near the chainsaw's, that nothing above Nyquist ever contributes, that
// the waveform is continuous across a block boundary and across a change in the
// partial count, and that two runs give the same bits.
//
// These tests print their measurements. The decay slope, the ladder error and the
// render cost are the acceptance evidence for issues #82 and #83, and a number
// that only exists inside a CHECK is a number nobody can quote.

using kart::core::Engine;
using kart::core::EngineAudioConfig;
using kart::core::EngineAudioInput;
using kart::core::EngineSynth;
namespace kz_audio = kart::core::kz_audio;

namespace {

// Magnitude of one DFT bin, rectangular window. Only ever called on buffers whose
// length is an exact whole number of fundamental periods, so there is no leakage
// to window away and a rectangular window is the honest choice: a Hanning window
// would spread each partial over three bins and blur the alias test's whole point.
double bin_magnitude(const std::vector<float> &samples, int bin) {
	const int count = static_cast<int>(samples.size());
	double real = 0.0;
	double imaginary = 0.0;
	const double step = -2.0 * 3.14159265358979323846 * static_cast<double>(bin) / static_cast<double>(count);
	for (int index = 0; index < count; ++index) {
		const double angle = step * static_cast<double>(index);
		real += static_cast<double>(samples[index]) * std::cos(angle);
		imaginary += static_cast<double>(samples[index]) * std::sin(angle);
	}
	return std::sqrt(real * real + imaginary * imaginary) * 2.0 / static_cast<double>(count);
}

// Render `frames` samples with the state held fixed, after letting the smoothers
// settle. Everything spectral in this file needs a steady state, and the one-pole
// controls take about six publish periods to get there.
void render_steady(EngineSynth &synth, const EngineAudioInput &input,
		std::vector<float> &out, int frames, double sample_rate) {
	synth.publish(input);
	const int settle_frames = static_cast<int>(sample_rate * 0.5);
	std::vector<float> scratch(512);
	for (int done = 0; done < settle_frames; done += 512) {
		synth.publish(input);
		synth.render(scratch.data(), 512);
	}
	out.assign(static_cast<std::size_t>(frames), 0.0f);
	synth.render(out.data(), frames);
}

// Least-squares slope of dB against log2 of harmonic number — the same fit
// `kz_audio_reference.h`'s DECAY_DB_PER_DOUBLING_* constants claim to be.
double fit_decay(const std::vector<double> &log2_harmonic, const std::vector<double> &db) {
	const std::size_t count = log2_harmonic.size();
	double mean_x = 0.0;
	double mean_y = 0.0;
	for (std::size_t index = 0; index < count; ++index) {
		mean_x += log2_harmonic[index];
		mean_y += db[index];
	}
	mean_x /= static_cast<double>(count);
	mean_y /= static_cast<double>(count);
	double covariance = 0.0;
	double variance = 0.0;
	for (std::size_t index = 0; index < count; ++index) {
		const double dx = log2_harmonic[index] - mean_x;
		covariance += dx * (db[index] - mean_y);
		variance += dx * dx;
	}
	return covariance / variance;
}

// --- Brightness, which needs every bin rather than three of them ---------------
//
// `bin_magnitude` above answers "how much is at this exact frequency", which is
// the right question for the ladder and the alias test and the wrong one for a
// spectral centroid. A centroid is a statistic over the whole spectrum including
// the broadband noise layer, so it needs a transform rather than a probe, and it
// has to be taken **on the shipped configuration** — noise on, comb on, jitter on
// — because the thing being asserted is what a driver hears and the noise layer
// is half of what went wrong.

constexpr int SPECTRUM_N = 32768; // 1.46 Hz bins at 48 kHz, 0.68 s of audio

// Iterative radix-2 Cooley-Tukey, in place. Small enough to read, and the
// alternative is pulling a dependency into a suite whose selling point is that it
// builds with one include path and no engine.
void fft_in_place(std::vector<double> &real, std::vector<double> &imaginary) {
	const int count = static_cast<int>(real.size());
	for (int i = 1, j = 0; i < count; ++i) {
		int bit = count >> 1;
		for (; (j & bit) != 0; bit >>= 1) {
			j ^= bit;
		}
		j ^= bit;
		if (i < j) {
			std::swap(real[i], real[j]);
			std::swap(imaginary[i], imaginary[j]);
		}
	}
	for (int length = 2; length <= count; length <<= 1) {
		const double angle = -2.0 * 3.14159265358979323846 / static_cast<double>(length);
		const double wr = std::cos(angle);
		const double wi = std::sin(angle);
		for (int start = 0; start < count; start += length) {
			double cr = 1.0;
			double ci = 0.0;
			for (int k = 0; k < length / 2; ++k) {
				const int a = start + k;
				const int b = a + length / 2;
				const double xr = real[b] * cr - imaginary[b] * ci;
				const double xi = real[b] * ci + imaginary[b] * cr;
				real[b] = real[a] - xr;
				imaginary[b] = imaginary[a] - xi;
				real[a] += xr;
				imaginary[a] += xi;
				const double nr = cr * wr - ci * wi;
				ci = cr * wi + ci * wr;
				cr = nr;
			}
		}
	}
}

struct Brightness {
	double centroid_hz = 0.0;
	double hf_fraction = 0.0;
	int partials = 0;
	double rms_dbfs = 0.0;
};

// Power-weighted mean frequency and the fraction of band power above
// `kz_audio::HF_SPLIT_HZ`, measured over `kz_audio::CENTROID_BAND_*`.
//
// **The band is not a detail.** A centroid without the band it was taken over is
// not a number anybody can reproduce, which is why those two edges are constants
// in `kz_audio_reference.h` rather than literals here — the same band was used on
// the recordings the comparison figures came from, and a gate that measured the
// synth over a different band would be comparing two different statistics and
// would pass or fail for arithmetic reasons.
Brightness brightness(const EngineAudioConfig &config, const EngineAudioInput &input,
		double sample_rate) {
	EngineSynth synth;
	synth.configure(config, sample_rate);
	std::vector<float> samples;
	render_steady(synth, input, samples, SPECTRUM_N, sample_rate);

	std::vector<double> real(SPECTRUM_N);
	std::vector<double> imaginary(SPECTRUM_N, 0.0);
	double sum_square = 0.0;
	for (int index = 0; index < SPECTRUM_N; ++index) {
		const double value = samples[index];
		sum_square += value * value;
		// Hann, matching the analysis the reference figures were taken with.
		const double window = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 *
										  static_cast<double>(index) / static_cast<double>(SPECTRUM_N));
		real[index] = value * window;
	}
	fft_in_place(real, imaginary);

	const double bin_hz = sample_rate / static_cast<double>(SPECTRUM_N);
	const int low = static_cast<int>(kz_audio::CENTROID_BAND_LOW_HZ / bin_hz);
	int high = static_cast<int>(kz_audio::CENTROID_BAND_HIGH_HZ / bin_hz);
	if (high > SPECTRUM_N / 2) {
		high = SPECTRUM_N / 2;
	}
	const int split = static_cast<int>(kz_audio::HF_SPLIT_HZ / bin_hz);

	double total = 0.0;
	double weighted = 0.0;
	double above = 0.0;
	for (int index = low; index < high; ++index) {
		const double power = real[index] * real[index] + imaginary[index] * imaginary[index];
		total += power;
		weighted += power * static_cast<double>(index) * bin_hz;
		if (index >= split) {
			above += power;
		}
	}

	Brightness out;
	out.centroid_hz = total > 0.0 ? weighted / total : 0.0;
	out.hf_fraction = total > 0.0 ? above / total : 0.0;
	out.partials = synth.partial_count();
	const double rms = std::sqrt(sum_square / static_cast<double>(SPECTRUM_N));
	out.rms_dbfs = rms > 0.0 ? 20.0 * std::log10(rms) : -200.0;
	return out;
}

// The exponent k in `centroid ~ f0^k`, between two rpm. This is the quantity
// `kz_audio::CENTROID_EXPONENT_*` carries and the reason it is an exponent rather
// than a ratio is in that constant's comment: every recording spans a different
// rev range, so a raw top-over-bottom figure is not comparable between two of
// them, and would not be comparable to the synth either.
double centroid_exponent(double centroid_low, double rpm_low, double centroid_high, double rpm_high) {
	return std::log2(centroid_high / centroid_low) / std::log2(rpm_high / rpm_low);
}

// A state with the engine pulling hard and nothing exceptional happening.
EngineAudioInput driving(double rpm, double load) {
	EngineAudioInput input;
	input.rpm = rpm;
	input.load = load;
	input.throttle = load;
	input.gear = 3;
	return input;
}

// The configuration the spectral tests use: no comb, no noise, a low gain so the
// soft clip is provably inactive. What is being measured is the harmonic stack,
// and the comb is a separate test.
EngineAudioConfig bare_stack() {
	EngineAudioConfig config;
	config.comb_depth = 0.0;
	config.noise_gain = 0.0;
	config.gain = 0.25;
	return config;
}

} // namespace

TEST_CASE("the realized ladder is the ladder that was measured") {
	// 6,000 rpm puts f0 at exactly 100 Hz and sits below the powerband, so the
	// on-pipe crossfade is closed and what comes out is PIPE_LADDER_DB alone.
	// 4,800 samples at 48 kHz is 0.1 s, which is exactly 10 fundamental periods,
	// so every harmonic lands exactly on a bin and the DFT has no leakage.
	const double sample_rate = 48000.0;
	const int frames = 4800;
	const double bins_per_harmonic = 10.0; // f0 100 Hz / bin width 10 Hz

	EngineSynth synth;
	synth.configure(bare_stack(), sample_rate);
	// A jittered fundamental has no exact bins to measure, so the spectral tests
	// switch the combustion variation off and measure the stack alone. That it is
	// on by default is asserted separately.
	synth.set_combustion_jitter(0.0);
	std::vector<float> samples;
	render_steady(synth, driving(6000.0, 1.0), samples, frames, sample_rate);

	const double reference = bin_magnitude(samples, static_cast<int>(bins_per_harmonic));
	REQUIRE(reference > 0.0);

	std::vector<double> log2_harmonic;
	std::vector<double> measured_db;
	double worst_error = 0.0;
	for (std::size_t point = 0; point < kz_audio::LADDER_POINTS; ++point) {
		const int harmonic = kz_audio::LADDER_INDEX[point];
		const int bin = static_cast<int>(bins_per_harmonic * static_cast<double>(harmonic));
		const double db = 20.0 * std::log10(bin_magnitude(samples, bin) / reference);
		const double error = std::fabs(db - kz_audio::PIPE_LADDER_DB[point]);
		if (error > worst_error) {
			worst_error = error;
		}
		log2_harmonic.push_back(std::log2(static_cast<double>(harmonic)));
		measured_db.push_back(db);
		MESSAGE("h" << harmonic << ": measured " << db << " dB re h1, table "
					<< kz_audio::PIPE_LADDER_DB[point] << " dB, error " << error);
	}
	MESSAGE("worst ladder error " << worst_error << " dB over the 12 measured indices");

	// 0.5 dB. The stack is a sum of pure sinusoids at exact bin centers, so the
	// only error sources are the sine table's -131 dB interpolation floor and the
	// residual of the control smoother after half a second of settling; anything
	// larger means the interpolation or the normalization is wrong.
	CHECK(worst_error < 0.5);

	// The negative control, and the whole reason `kz_audio_reference.h` exists.
	//
	// A synthesizer's default harmonic stack is 1/n, which is -6.02 dB per
	// doubling, and the one engine in the corpus with an ordinary muffler fits at
	// -6.7. If this number lands anywhere near either of those, the thing being
	// synthesized is a chainsaw.
	//
	// Note what the measured slope is *not*: it is not
	// `DECAY_DB_PER_DOUBLING_PIPE` (-2.95). That constant is the fit reported over
	// the full unthinned analysis, and the thinned 12-point table it is carried
	// beside fits at about -1.9 on its own. The realized spectrum follows the
	// table, so it follows the table's slope. See the report on
	// `kz_audio_reference.h` for why that matters past h24.
	const double slope = fit_decay(log2_harmonic, measured_db);
	MESSAGE("realized decay " << slope << " dB per doubling of harmonic number "
							  << "(muffler control " << kz_audio::DECAY_DB_PER_DOUBLING_MUFFLER
							  << ", 1/n " << kz_audio::DECAY_DB_PER_DOUBLING_ONE_OVER_N << ")");
	CHECK(slope > kz_audio::DECAY_DB_PER_DOUBLING_MUFFLER + 2.0);
	CHECK(slope > kz_audio::DECAY_DB_PER_DOUBLING_ONE_OVER_N + 2.0);
	// And a racing two-stroke does not rise, either.
	CHECK(slope < 0.0);
	// Within 10 dB of the fundamental at h24 is the headline claim of the whole
	// measurement. The realized spectrum has to keep it.
	CHECK(measured_db.back() > -10.5);
}

TEST_CASE("the note brightens with rpm the way a tuned pipe does, not the way a muffler does") {
	// **The gate for "not enough two-stroke scream", and the one that would have
	// caught it.** Every other spectral test in this file measures the synth at an
	// operating point. None of them said anything about how the spectrum *moves*
	// with rpm, and that motion is what separates a racing two-stroke from a
	// chainsaw — `kz_audio::CENTROID_EXPONENT_*` measures three tuned-pipe engines
	// at k = 0.49 to 1.27 and two muffled ones at 0.09 and 0.15, with no overlap.
	//
	// What the shipped build measured before this gate existed: k = 0.21 across the
	// driving range and **k = -0.33 inside the powerband**, where the note got
	// duller the harder it was revved. That is on the wrong side of both negative
	// controls, and every one of the eleven test cases around this one passed.
	//
	// Measured on the **shipped configuration** — noise, comb and combustion jitter
	// all on — because the noise layer was two thirds of the defect and a bare
	// stack would have hidden it. The stack alone rose at k = 0.71 while the mix
	// rose at 0.21.
	const EngineAudioConfig config; // shipped defaults, deliberately not bare_stack()
	const double RATE = 48000.0;

	const double RPMS[] = { 3000.0, 4500.0, 6000.0, 7500.0, 9000.0, 10500.0, 12000.0, 13000.0, 14000.0 };
	const int COUNT = static_cast<int>(sizeof(RPMS) / sizeof(RPMS[0]));

	MESSAGE("");
	MESSAGE("Spectral centroid and high-frequency share against rpm, shipped config.");
	char line[220];
	std::snprintf(line, sizeof(line), "%8s %7s %8s %10s %10s %10s",
			"rpm", "f0 Hz", "partials", "centroid", ">4 kHz %", "rms dBFS");
	MESSAGE(line);

	std::vector<Brightness> measured;
	for (int index = 0; index < COUNT; ++index) {
		const Brightness b = brightness(config, driving(RPMS[index], 1.0), RATE);
		measured.push_back(b);
		std::snprintf(line, sizeof(line), "%8.0f %7.1f %8d %10.0f %10.2f %10.2f",
				RPMS[index], kz_audio::rpm_to_f0_hz(RPMS[index]), b.partials,
				b.centroid_hz, 100.0 * b.hf_fraction, b.rms_dbfs);
		MESSAGE(line);
	}

	// Index 2 is 6,000 rpm — below the powerband, off the pipe — and index 7 is
	// 13,000, which is `kz::PEAK_POWER_RPM`. That span is the closest thing the
	// synth has to the rev sweeps the corpus recordings were measured over.
	const double k_range = centroid_exponent(measured[2].centroid_hz, RPMS[2],
			measured[7].centroid_hz, RPMS[7]);
	// And the same question asked only inside the powerband, which is the range a
	// driver actually spends time in and the range the old build failed hardest.
	const double k_band = centroid_exponent(measured[4].centroid_hz, RPMS[4],
			measured[7].centroid_hz, RPMS[7]);

	MESSAGE("");
	MESSAGE("centroid exponent k, 6,000 to 13,000 rpm: " << k_range);
	MESSAGE("centroid exponent k, 9,000 to 13,000 rpm (powerband only): " << k_band);
	MESSAGE("measured corpus: racing pipes " << kz_audio::CENTROID_EXPONENT_D7 << " to "
			<< kz_audio::CENTROID_EXPONENT_COLIBRI << ", mufflers "
			<< kz_audio::CENTROID_EXPONENT_CHAINSAW << " and "
			<< kz_audio::CENTROID_EXPONENT_FOUR_STROKE);

	// The band, from both sides. Below it the synth is a muffler; above it the
	// brightness sweep is steeper than anything anybody measured, which would be
	// inventing an engine rather than reproducing one.
	CHECK(k_range >= kz_audio::CENTROID_EXPONENT_MIN);
	CHECK(k_range <= kz_audio::CENTROID_EXPONENT_MAX);
	CHECK(k_band >= kz_audio::CENTROID_EXPONENT_MIN);
	CHECK(k_band <= kz_audio::CENTROID_EXPONENT_MAX);

	// The negative control, stated separately from the band even though it is
	// implied by it. The two muffled recordings are the failure this gate exists
	// for and a reader should be able to see them asserted against by name.
	CHECK(k_range > kz_audio::CENTROID_EXPONENT_MUFFLED_MAX);
	CHECK(k_band > kz_audio::CENTROID_EXPONENT_MUFFLED_MAX);

	// Monotone: the note must brighten at every step through the powerband, not
	// merely end up brighter. A synth that dipped in the middle and recovered would
	// satisfy the exponents above and would sound like it was changing its mind.
	for (int index = 4; index < 7; ++index) {
		CHECK(measured[index + 1].centroid_hz > measured[index].centroid_hz);
	}

	// The high-frequency share at peak power, against the sanity bound rather than
	// against a target. `kz_audio::HF_FRACTION_ONPIPE_*`'s comment says why there
	// is no target: the corpus spans 6.6% to 42% and the spread is microphone
	// distance, so picking a point inside it would be picking a distance.
	MESSAGE("high-frequency share at 13,000 rpm: " << 100.0 * measured[7].hf_fraction
			<< " %, corpus band " << 100.0 * kz_audio::HF_FRACTION_ONPIPE_MIN << " to "
			<< 100.0 * kz_audio::HF_FRACTION_ONPIPE_MAX << " %");
	CHECK(measured[7].hf_fraction >= kz_audio::HF_FRACTION_ONPIPE_MIN);
	CHECK(measured[7].hf_fraction <= kz_audio::HF_FRACTION_ONPIPE_MAX);
}

TEST_CASE("the stack ends at a harmonic count, which is what makes the spectrum slide with rpm") {
	// The mechanism under the gate above, asserted directly. A centroid that failed
	// to rise could be the ladder or could be the ceiling, and a test that only
	// measured the spectrum would not say which.
	EngineSynth synth;
	synth.configure(EngineAudioConfig(), 48000.0);

	MESSAGE("");
	MESSAGE("Stack ceiling against rpm.");
	char line[200];
	std::snprintf(line, sizeof(line), "%8s %8s %11s %10s", "rpm", "f0 Hz", "ceiling Hz", "harmonics");
	MESSAGE(line);

	// Between the floor and the absolute cap the ceiling is exactly a harmonic
	// count, which is the whole claim: the spectrum's shape is fixed in harmonic
	// space, so it moves bodily up with the fundamental.
	const double UNCLAMPED[] = { 4000.0, 6000.0, 9000.0, 12000.0, 14000.0, 14800.0 };
	for (double rpm : UNCLAMPED) {
		const double f0 = kz_audio::rpm_to_f0_hz(rpm);
		const double ceiling = synth.stack_ceiling_hz(f0);
		std::snprintf(line, sizeof(line), "%8.0f %8.1f %11.0f %10.1f", rpm, f0, ceiling, ceiling / f0);
		MESSAGE(line);
		CHECK(ceiling / f0 == doctest::Approx(kart::core::synth_tuning::STACK_CEILING_HARMONICS).epsilon(1e-9));
	}

	// `Engine::hard_cut_rpm` is 14,800 and the absolute cap must sit clear of it,
	// or the cap is shaping the note over the top of the range instead of catching
	// a runaway. This is the assertion that would fire if somebody lowered
	// `kz_audio::STACK_CEILING_HZ` back to the 8 kHz that was clipping the top
	// 800 rpm of the powerband.
	const Engine engine;
	const double f0_cut = kz_audio::rpm_to_f0_hz(engine.hard_cut_rpm);
	MESSAGE("at hard_cut_rpm " << engine.hard_cut_rpm << " the harmonic ceiling wants "
			<< kart::core::synth_tuning::STACK_CEILING_HARMONICS * f0_cut
			<< " Hz and the absolute cap is " << kz_audio::STACK_CEILING_HZ);
	CHECK(kart::core::synth_tuning::STACK_CEILING_HARMONICS * f0_cut < kz_audio::STACK_CEILING_HZ);

	// The floor, below the powerband, and the cap, past any rpm the engine reaches.
	CHECK(synth.stack_ceiling_hz(kz_audio::rpm_to_f0_hz(2000.0)) ==
			doctest::Approx(kart::core::synth_tuning::STACK_CEILING_MIN_HZ).epsilon(1e-9));
	CHECK(synth.stack_ceiling_hz(kz_audio::rpm_to_f0_hz(30000.0)) ==
			doctest::Approx(kz_audio::STACK_CEILING_HZ).epsilon(1e-9));

	// And the consequence that pays for it twice: a stack whose size barely moves
	// costs a predictable amount and stops the amplitude-sum normalization from
	// swinging the stack against the noise layer across the rev range. It was 160
	// partials at 3,000 rpm and 36 at 13,000, a 6.5 dB swing in the balance between
	// two layers for no reason anybody modelled.
	EngineSynth low;
	EngineSynth high;
	low.configure(EngineAudioConfig(), 48000.0);
	high.configure(EngineAudioConfig(), 48000.0);
	std::vector<float> scratch;
	render_steady(low, driving(6000.0, 1.0), scratch, 512, 48000.0);
	render_steady(high, driving(13000.0, 1.0), scratch, 512, 48000.0);
	MESSAGE("partials: " << low.partial_count() << " at 6,000 rpm, "
			<< high.partial_count() << " at 13,000");
	CHECK(low.partial_count() - high.partial_count() <= 2);
}

TEST_CASE("past the measured ladder the extrapolation follows the tail, not the hump") {
	// `kz_audio::DECAY_DB_PER_DOUBLING_*_TAIL` claims to be a least-squares fit of
	// a specific slice of a specific table. This recomputes both from the tables
	// themselves, so the constants cannot drift away from the ladders they came out
	// of — the failure mode being that somebody edits a ladder and the decay beside
	// it silently keeps describing the old one.
	std::vector<double> log2_harmonic;
	std::vector<double> db;

	// The slice is `kz_audio::LADDER_TAIL_FROM` to h24 — the upper six tabulated
	// indices, which is the split `LADDER_INDEX`'s comment says the table was
	// thinned on. An earlier version of this fitted "from each table's own
	// maximum", which sounds equivalent and is not: the pipe ladder is flat to
	// within 0.5 dB out to h8 so its argmax is h1, and that rule handed back the
	// whole-range fit for the one ladder the change was about. This test failed on
	// it, which is the only reason it is not still in the tree.
	auto tail_fit = [&](const double *ladder) {
		log2_harmonic.clear();
		db.clear();
		for (std::size_t index = 0; index < kz_audio::LADDER_POINTS; ++index) {
			if (kz_audio::LADDER_INDEX[index] < kz_audio::LADDER_TAIL_FROM) {
				continue;
			}
			log2_harmonic.push_back(std::log2(static_cast<double>(kz_audio::LADDER_INDEX[index])));
			db.push_back(ladder[index]);
		}
		return fit_decay(log2_harmonic, db);
	};

	const double pipe_tail = tail_fit(kz_audio::PIPE_LADDER_DB);
	const double onpipe_tail = tail_fit(kz_audio::ONPIPE_LADDER_DB);

	MESSAGE("");
	MESSAGE("pipe ladder tail fit " << pipe_tail << " against the constant "
			<< kz_audio::DECAY_DB_PER_DOUBLING_PIPE_TAIL);
	MESSAGE("on-pipe ladder tail fit " << onpipe_tail << " against the constant "
			<< kz_audio::DECAY_DB_PER_DOUBLING_ONPIPE_TAIL);

	CHECK(pipe_tail == doctest::Approx(kz_audio::DECAY_DB_PER_DOUBLING_PIPE_TAIL).epsilon(0.005));
	CHECK(onpipe_tail == doctest::Approx(kz_audio::DECAY_DB_PER_DOUBLING_ONPIPE_TAIL).epsilon(0.005));

	// The point of the whole change: the on-pipe ladder has been falling for two
	// octaves by the time the table runs out, so an extrapolation past it must
	// fall too. The whole-range fit does not — it is +1.04, because it is a line
	// through a hump — and using it made every partial above h24 louder than the
	// one below it.
	MESSAGE("on-pipe whole-range fit is " << kz_audio::DECAY_DB_PER_DOUBLING_ONPIPE
			<< " dB per doubling, which extrapolates in the wrong direction");
	CHECK(onpipe_tail < 0.0);
	CHECK(kz_audio::DECAY_DB_PER_DOUBLING_ONPIPE > 0.0); // the trap, kept visible

	// And the realized stack agrees: h48 must sit below h24 on both ladders.
	const double pipe_h24 = EngineSynth::ladder_db(kz_audio::PIPE_LADDER_DB, 24.0,
			kz_audio::DECAY_DB_PER_DOUBLING_PIPE_TAIL);
	const double pipe_h48 = EngineSynth::ladder_db(kz_audio::PIPE_LADDER_DB, 48.0,
			kz_audio::DECAY_DB_PER_DOUBLING_PIPE_TAIL);
	const double on_h24 = EngineSynth::ladder_db(kz_audio::ONPIPE_LADDER_DB, 24.0,
			kz_audio::DECAY_DB_PER_DOUBLING_ONPIPE_TAIL);
	const double on_h48 = EngineSynth::ladder_db(kz_audio::ONPIPE_LADDER_DB, 48.0,
			kz_audio::DECAY_DB_PER_DOUBLING_ONPIPE_TAIL);
	MESSAGE("pipe h24 " << pipe_h24 << " -> h48 " << pipe_h48
			<< " dB;  on-pipe h24 " << on_h24 << " -> h48 " << on_h48);
	CHECK(pipe_h48 < pipe_h24);
	CHECK(on_h48 < on_h24);
}

TEST_CASE("the rpm level term is an attenuation and can never cost headroom") {
	// `synth_tuning::RPM_LEVEL_RANGE_DB`'s comment makes a safety claim — that
	// anchoring the level rise at peak power and applying it downward means the
	// loudest output the synth can produce is exactly what it produced before the
	// term existed. That claim is the reason this term did not need a new headroom
	// budget, so it is asserted rather than trusted.
	MESSAGE("");
	MESSAGE("rpm level term, dB relative to peak power.");
	char line[160];
	const double RPMS[] = { 0.0, 500.0, 2000.0, 4000.0, 6000.0, 9000.0, 11000.0,
		13000.0, 14000.0, 14800.0, 20000.0 };
	const int COUNT = static_cast<int>(sizeof(RPMS) / sizeof(RPMS[0]));
	double previous = -1e9;
	for (int index = 0; index < COUNT; ++index) {
		const double db = EngineSynth::rpm_level_db(RPMS[index]);
		std::snprintf(line, sizeof(line), "  %8.0f rpm  %7.2f dB", RPMS[index], db);
		MESSAGE(line);

		// Never positive: the peak cannot move.
		CHECK(db <= 0.0);
		// Never past the declared range.
		CHECK(db >= -kart::core::synth_tuning::RPM_LEVEL_RANGE_DB);
		// Monotone non-decreasing, including through the clamp and through zero rpm,
		// which is the argument that a rev sweep never gets quieter as it rises.
		CHECK(db >= previous - 1e-12);
		previous = db;
	}

	// Exactly zero at the anchor. `Approx(0).epsilon(0)` compares 0 < 0 and fails,
	// so this is written as an absolute bound.
	CHECK(std::fabs(EngineSynth::rpm_level_db(kart::core::kz::PEAK_POWER_RPM)) < 1e-12);

	// The floor bites below the powerband and not inside it, which is what makes
	// the clamp invisible to a driver.
	CHECK(EngineSynth::rpm_level_db(kart::core::kz::POWERBAND_MIN_RPM) >
			-kart::core::synth_tuning::RPM_LEVEL_RANGE_DB);
}

TEST_CASE("coming on the pipe changes the ladder and is not a volume knob") {
	const double sample_rate = 48000.0;
	const int frames = 4800;

	// Same f0, two load states either side of the powerband would change f0 too,
	// so instead the same *rpm* is rendered at zero load and at full load. At
	// 12,000 rpm — inside kz::POWERBAND_MIN_RPM..MAX_RPM — full load opens the
	// on-pipe crossfade all the way and zero load closes it.
	const double rpm = 12000.0; // f0 200 Hz, 40 periods in 4800 samples at 48 kHz
	const double bins_per_harmonic = 20.0;

	EngineSynth off_pipe;
	off_pipe.configure(bare_stack(), sample_rate);
	off_pipe.set_combustion_jitter(0.0);
	std::vector<float> off_samples;
	render_steady(off_pipe, driving(rpm, 0.0), off_samples, frames, sample_rate);

	EngineSynth on_pipe;
	on_pipe.configure(bare_stack(), sample_rate);
	on_pipe.set_combustion_jitter(0.0);
	std::vector<float> on_samples;
	render_steady(on_pipe, driving(rpm, 1.0), on_samples, frames, sample_rate);

	const double off_h1 = bin_magnitude(off_samples, static_cast<int>(bins_per_harmonic));
	const double on_h1 = bin_magnitude(on_samples, static_cast<int>(bins_per_harmonic));
	const double off_h6 = bin_magnitude(off_samples, static_cast<int>(bins_per_harmonic * 6.0));
	const double on_h6 = bin_magnitude(on_samples, static_cast<int>(bins_per_harmonic * 6.0));

	const double off_ratio = 20.0 * std::log10(off_h6 / off_h1);
	const double on_ratio = 20.0 * std::log10(on_h6 / on_h1);
	MESSAGE("h6 re h1: off the pipe " << off_ratio << " dB, on the pipe " << on_ratio
									  << " dB, change " << (on_ratio - off_ratio) << " dB");

	// The table says h6 sits at -0.70 dB on the pipe ladder's off-pipe end and
	// +10.3 on the D-9's, an 11 dB swing. "Clearly audible" is #82's word and 11 dB
	// of midrange is what it means. Half of it is the floor here.
	CHECK((on_ratio - off_ratio) > 5.0);
	CHECK(std::fabs(off_ratio - kz_audio::PIPE_LADDER_DB[5]) < 0.5);
	CHECK(std::fabs(on_ratio - kz_audio::ONPIPE_LADDER_DB[5]) < 0.5);
}

TEST_CASE("no partial above Nyquist ever contributes") {
	// The bookkeeping half. Sweeping past the hard cut with a ceiling above
	// Nyquist puts the two rules in direct conflict at every rpm, and Nyquist has
	// to win every time.
	for (const double sample_rate : { 44100.0, 48000.0 }) {
		EngineAudioConfig config = bare_stack();
		config.stack_ceiling_hz = 30000.0; // deliberately above Nyquist at both rates
		EngineSynth synth;
		synth.configure(config, sample_rate);

		double highest = 0.0;
		std::vector<float> block(256);
		for (double rpm = 0.0; rpm <= 40000.0; rpm += 250.0) {
			synth.publish(driving(rpm, 1.0));
			for (int repeat = 0; repeat < 8; ++repeat) {
				synth.render(block.data(), 256);
			}
			for (int index = 0; index < synth.partial_count(); ++index) {
				if (synth.partial_gain(index) == 0.0) {
					continue;
				}
				// `partial_frequency` reports the nominal, un-jittered frequency, so
				// the assertion has to carry the jitter's own headroom with it.
				const double frequency = synth.partial_frequency(index) * (1.0 + synth.combustion_jitter());
				CHECK(frequency < synth.nyquist_hz());
				if (frequency > highest) {
					highest = frequency;
				}
			}
		}
		MESSAGE("sample rate " << sample_rate << ": highest contributing partial "
							   << highest << " Hz, Nyquist " << (sample_rate * 0.5));
		CHECK(highest < sample_rate * 0.5);
	}
}

TEST_CASE("nothing folds back into the audible band") {
	// The output half of the same assertion, and the one that would actually catch
	// a metallic buzz. A DFT cannot tell a partial at 23.8 kHz that folded down
	// from 24.2 kHz apart from a legitimate one at 23.8 kHz — unless the
	// fundamental is chosen so that the fold lands somewhere that is not a
	// harmonic of it.
	//
	// 66,000 rpm is f0 = 1,100 Hz, far past `Engine::hard_cut_rpm`, and it is
	// chosen because 48,000 is not a multiple of 1,100: a partial above Nyquist
	// folds to 48000 - h*1100, which is 70 Hz off the nearest harmonic slot and
	// therefore visible. 4,800 samples is exactly 110 periods, so the spectrum of
	// a correct render is zero everywhere except at multiples of bin 110.
	const double sample_rate = 48000.0;
	const int frames = 4800;
	const int fundamental_bin = 110;

	EngineAudioConfig config = bare_stack();
	config.stack_ceiling_hz = 30000.0;
	EngineSynth synth;
	synth.configure(config, sample_rate);
	synth.set_combustion_jitter(0.0);
	std::vector<float> samples;
	render_steady(synth, driving(66000.0, 0.0), samples, frames, sample_rate);

	const double reference = bin_magnitude(samples, fundamental_bin);
	REQUIRE(reference > 0.0);

	double worst_non_harmonic = 0.0;
	int worst_bin = 0;
	for (int bin = 1; bin < frames / 2; ++bin) {
		if (bin % fundamental_bin == 0) {
			continue;
		}
		const double magnitude = bin_magnitude(samples, bin) / reference;
		if (magnitude > worst_non_harmonic) {
			worst_non_harmonic = magnitude;
			worst_bin = bin;
		}
	}
	const double worst_db = 20.0 * std::log10(worst_non_harmonic);
	MESSAGE("loudest non-harmonic bin " << (worst_bin * 10) << " Hz at " << worst_db
										<< " dB re h1; partials rendered " << synth.partial_count()
										<< ", top at " << synth.partial_frequency(synth.partial_count() - 1) << " Hz");

	// -50 dB. A single folded partial would arrive at roughly the level of its
	// neighbors, which on this ladder is within 10 dB of h1 — sixty times louder
	// than this threshold, so there is no ambiguity about what a failure means.
	CHECK(worst_db < -50.0);
}

TEST_CASE("partials fade in and out rather than switching") {
	// #82's first acceptance criterion is "no audible stepping anywhere in the
	// range", and the way a harmonic-stack synth fails it that has nothing to do
	// with pitch is a partial appearing at full gain when the count grows. So:
	// sweep rpm across the whole range and, every time the count changes, look at
	// what the arriving or departing partial was actually worth.
	const double sample_rate = 48000.0;
	EngineSynth synth;
	synth.configure(bare_stack(), sample_rate);

	std::vector<float> block(256);
	// Settle at the bottom of the sweep first. The transient from silence is a
	// level fade-in and not a partial-count step, and measuring it here would be
	// measuring the wrong thing.
	for (int repeat = 0; repeat < 200; ++repeat) {
		synth.publish(driving(800.0, 1.0));
		synth.render(block.data(), 256);
	}

	// What is actually asserted: **the partial at the top of the stack is always
	// quiet**. That is the invariant the fade exists to produce, and it covers both
	// directions at once — a partial that is quiet when it is on top is quiet when
	// it arrives and quiet when it leaves, whichever way the count is moving.
	// Checking only the indices that changed would miss every departure, because a
	// departed partial is no longer in the stack to look at.
	double worst_step = 0.0;
	double worst_rpm = 0.0;
	int lowest_count = EngineSynth::MAX_PARTIALS + 1;
	int highest_count = 0;
	for (double rpm = 800.0; rpm <= 15000.0; rpm += 10.0) {
		synth.publish(driving(rpm, 1.0));
		synth.render(block.data(), 256);

		const int count = synth.partial_count();
		if (count < 2) {
			continue;
		}
		if (count < lowest_count) {
			lowest_count = count;
		}
		if (count > highest_count) {
			highest_count = count;
		}
		double loudest = 0.0;
		for (int index = 0; index < count; ++index) {
			const double gain = std::fabs(synth.partial_gain(index));
			if (gain > loudest) {
				loudest = gain;
			}
		}
		if (loudest <= 0.0) {
			continue;
		}
		const double relative = std::fabs(synth.partial_gain(count - 1)) / loudest;
		if (relative > worst_step) {
			worst_step = relative;
			worst_rpm = rpm;
		}
	}
	MESSAGE("stack size ran from " << lowest_count << " to " << highest_count
								   << " partials over the sweep; the top partial was never louder than "
								   << (worst_step * 100.0) << "% of the loudest, worst at " << worst_rpm << " rpm");
	// The stack really does change size over the sweep, or the assertion above is
	// checking nothing.
	CHECK(highest_count > lowest_count + 20);
	// 5% is -26 dB against the loudest partial in a stack of dozens. Without the
	// ceiling fade this number is 100% by construction, so the test has an enormous
	// margin between pass and the failure it is looking for.
	CHECK(worst_step < 0.05);
}

TEST_CASE("the waveform is continuous across a block boundary with rpm moving") {
	// Snapping frequency at block boundaries is the audible stepping the same
	// criterion forbids, and it is invisible in a spectrum taken over one block.
	// What it shows up in is the first difference at the join.
	const double sample_rate = 48000.0;
	const int frames = 400; // one 120 Hz publish period at 48 kHz
	EngineSynth synth;
	synth.configure(bare_stack(), sample_rate);

	// Settle at 9,000, then hand it a 3,000 rpm step — far larger than any real
	// tick delivers, because the point is to make the failure loud if it exists.
	std::vector<float> scratch(frames);
	for (int repeat = 0; repeat < 200; ++repeat) {
		synth.publish(driving(9000.0, 1.0));
		synth.render(scratch.data(), frames);
	}

	std::vector<float> first(frames);
	std::vector<float> second(frames);
	synth.publish(driving(9000.0, 1.0));
	synth.render(first.data(), frames);
	synth.publish(driving(12000.0, 1.0));
	synth.render(second.data(), frames);

	double interior = 0.0;
	for (int index = 1; index < frames; ++index) {
		const double a = std::fabs(static_cast<double>(first[index]) - static_cast<double>(first[index - 1]));
		const double b = std::fabs(static_cast<double>(second[index]) - static_cast<double>(second[index - 1]));
		if (a > interior) {
			interior = a;
		}
		if (b > interior) {
			interior = b;
		}
	}
	const double join = std::fabs(static_cast<double>(second[0]) - static_cast<double>(first[frames - 1]));
	MESSAGE("first difference: largest interior " << interior << ", at the join " << join
												  << " (ratio " << (join / interior) << ")");
	CHECK(join <= interior * 1.2);

	// The second difference is the one a snapped frequency actually breaks — a
	// phase accumulator keeps the signal continuous through a frequency step but
	// not its slope.
	double interior_second = 0.0;
	for (int index = 2; index < frames; ++index) {
		const double a = std::fabs(static_cast<double>(first[index]) - 2.0 * static_cast<double>(first[index - 1]) + static_cast<double>(first[index - 2]));
		const double b = std::fabs(static_cast<double>(second[index]) - 2.0 * static_cast<double>(second[index - 1]) + static_cast<double>(second[index - 2]));
		if (a > interior_second) {
			interior_second = a;
		}
		if (b > interior_second) {
			interior_second = b;
		}
	}
	const double join_second = std::fabs(static_cast<double>(second[0]) - 2.0 * static_cast<double>(first[frames - 1]) + static_cast<double>(first[frames - 2]));
	MESSAGE("second difference: largest interior " << interior_second << ", at the join " << join_second);
	CHECK(join_second <= interior_second * 1.2);
}

TEST_CASE("output is finite and bounded everywhere, at both device rates") {
	const Engine engine;
	for (const double sample_rate : { 44100.0, 48000.0 }) {
		EngineAudioConfig config; // the shipping defaults: comb, noise, gain 0.35
		EngineSynth synth;
		synth.configure(config, sample_rate);

		std::vector<float> block(512);
		double peak = 0.0;
		int state = 0;
		for (double rpm = 0.0; rpm <= engine.hard_cut_rpm + 4000.0; rpm += 100.0) {
			// Rotate through every state #83 wants audible, so that the bound is a
			// bound on the whole machine and not on the quiet half of it.
			EngineAudioInput input = driving(rpm, 1.0);
			input.trailing = (state % 5) == 1;
			input.shifting = (state % 5) == 2;
			input.on_limiter = (state % 5) == 3;
			input.over_rev = (state % 5) == 4;
			input.clutch_slip = ((state % 3) == 0) ? 400.0 : 0.0;
			++state;

			synth.publish(input);
			for (int repeat = 0; repeat < 4; ++repeat) {
				synth.render(block.data(), 512);
				for (const float sample : block) {
					REQUIRE(std::isfinite(sample));
					const double magnitude = std::fabs(static_cast<double>(sample));
					if (magnitude > peak) {
						peak = magnitude;
					}
					// The soft clip is asymptotic to 1.0, so this is a guarantee and
					// not an observation: whatever a caller does with `gain`, the
					// mixer never sees a sample outside the interval.
					CHECK(magnitude < 1.0);
				}
			}
		}
		MESSAGE("sample rate " << sample_rate << ": peak |sample| " << peak);
		CHECK(peak > 0.05); // and it is not silently doing nothing
	}
}

TEST_CASE("every state #83 wants audible actually changes the output") {
	const double sample_rate = 48000.0;
	const int frames = 4800;

	auto energy = [](const std::vector<float> &samples) {
		double sum = 0.0;
		for (const float sample : samples) {
			sum += static_cast<double>(sample) * static_cast<double>(sample);
		}
		return std::sqrt(sum / static_cast<double>(samples.size()));
	};

	EngineAudioConfig config;
	const double rpm = 12000.0;

	std::vector<float> plain;
	{
		EngineSynth synth;
		synth.configure(config, sample_rate);
		render_steady(synth, driving(rpm, 1.0), plain, frames, sample_rate);
	}

	// Trailing: quieter, and relatively brighter — the measured tilt.
	std::vector<float> trailing;
	{
		EngineSynth synth;
		synth.configure(config, sample_rate);
		EngineAudioInput input = driving(rpm, 0.0);
		input.trailing = true;
		render_steady(synth, input, trailing, frames, sample_rate);
	}

	std::vector<float> shifting;
	{
		EngineSynth synth;
		synth.configure(config, sample_rate);
		EngineAudioInput input = driving(rpm, 1.0);
		input.shifting = true;
		render_steady(synth, input, shifting, frames, sample_rate);
	}

	std::vector<float> limiter;
	{
		EngineSynth synth;
		synth.configure(config, sample_rate);
		EngineAudioInput input = driving(14500.0, 1.0);
		input.on_limiter = true;
		render_steady(synth, input, limiter, frames, sample_rate);
	}

	std::vector<float> over_rev;
	{
		EngineSynth synth;
		synth.configure(config, sample_rate);
		EngineAudioInput input = driving(15200.0, 1.0);
		input.over_rev = true;
		render_steady(synth, input, over_rev, frames, sample_rate);
	}

	std::vector<float> clutch;
	{
		EngineSynth synth;
		synth.configure(config, sample_rate);
		EngineAudioInput input = driving(6000.0, 1.0);
		input.clutch_slip = 300.0;
		render_steady(synth, input, clutch, frames, sample_rate);
	}

	std::vector<float> clutch_locked;
	{
		EngineSynth synth;
		synth.configure(config, sample_rate);
		render_steady(synth, driving(6000.0, 1.0), clutch_locked, frames, sample_rate);
	}

	MESSAGE("RMS: driving " << energy(plain) << ", trailing " << energy(trailing)
							<< ", shifting " << energy(shifting) << ", on the limiter "
							<< energy(limiter) << ", over-revving " << energy(over_rev)
							<< ", clutch slipping " << energy(clutch) << " (locked "
							<< energy(clutch_locked) << ")");

	// Trailing is quieter than driving. Level and tilt both point that way.
	CHECK(energy(trailing) < energy(plain));
	// A shift's torque cut is a duck, and a deep one.
	CHECK(energy(shifting) < energy(plain) * 0.6);
	// The limiter's chatter removes drive periodically, so its mean level falls.
	CHECK(energy(limiter) < energy(plain));
	// An over-rev is not quieter. That is the point of it being a different sound
	// from the limiter: one is the engine working and the other is money leaving.
	CHECK(energy(over_rev) > energy(limiter));
	// A slipping clutch adds a broadband layer that a locked one does not have.
	CHECK(energy(clutch) > energy(clutch_locked));
}

TEST_CASE("the comb is gentle, the way the measurement says it is") {
	// `kz_audio::COMB_RIPPLE_DB_MIN`..`MAX` is 1.6 to 2.6 dB RMS, and that is the
	// one number in the reference that contradicts a first guess hard enough to be
	// worth a test of its own: the deep metallic comb a synth reaches for is
	// nothing like this.
	const double sample_rate = 48000.0;
	const int frames = 4800;
	const double bins_per_harmonic = 10.0; // 6,000 rpm, f0 100 Hz

	// Sample the comb's response where the harmonics are, which is where it is
	// audible. 40 partials at 100 Hz spacing against a 704 Hz comb spacing covers
	// the ripple several times over.
	auto spectrum = [&](double depth) {
		EngineAudioConfig config = bare_stack();
		config.comb_depth = depth;
		EngineSynth synth;
		synth.configure(config, sample_rate);
		synth.set_combustion_jitter(0.0);
		std::vector<float> samples;
		render_steady(synth, driving(6000.0, 1.0), samples, frames, sample_rate);
		std::vector<double> magnitudes;
		for (int harmonic = 1; harmonic <= 40; ++harmonic) {
			magnitudes.push_back(bin_magnitude(samples,
					static_cast<int>(bins_per_harmonic * static_cast<double>(harmonic))));
		}
		return magnitudes;
	};

	const std::vector<double> dry = spectrum(0.0);

	// Ripple is the deviation of the response from its own mean, not the response
	// itself. The comb is normalized by 1/(1 + depth) so that it cannot raise the
	// peak, and that normalization is a broadband -2.0 dB at the default depth —
	// counting it as ripple reports 2.54 dB where the filter's actual ripple is
	// 1.53, which is the difference between landing inside the measured band and
	// landing under it. A constant gain is not a comb.
	auto ripple_of = [&](double depth) {
		const std::vector<double> wet = spectrum(depth);
		std::vector<double> response;
		for (std::size_t index = 0; index < wet.size(); ++index) {
			if (dry[index] <= 0.0 || wet[index] <= 0.0) {
				continue;
			}
			response.push_back(20.0 * std::log10(wet[index] / dry[index]));
		}
		double mean = 0.0;
		for (const double value : response) {
			mean += value;
		}
		mean /= static_cast<double>(response.size());
		double sum_squares = 0.0;
		for (const double value : response) {
			sum_squares += (value - mean) * (value - mean);
		}
		return std::sqrt(sum_squares / static_cast<double>(response.size()));
	};

	const double defaulted = ripple_of(EngineAudioConfig().comb_depth);
	MESSAGE("comb ripple at the default depth " << EngineAudioConfig().comb_depth << ": "
												<< defaulted << " dB RMS (measured band "
												<< kz_audio::COMB_RIPPLE_DB_MIN << " to "
												<< kz_audio::COMB_RIPPLE_DB_MAX << ")");

	// Which depths land inside the measured band. This is here because the answer
	// is not the default: `EngineAudioConfig::comb_depth` ships at 0.25 and that
	// sits just under the floor of the band it is meant to reproduce.
	bool any_inside = false;
	for (const double depth : { 0.20, 0.25, 0.30, 0.35, 0.40, 0.45 }) {
		const double ripple = ripple_of(depth);
		const bool inside = ripple >= kz_audio::COMB_RIPPLE_DB_MIN && ripple <= kz_audio::COMB_RIPPLE_DB_MAX;
		any_inside = any_inside || inside;
		MESSAGE("  depth " << depth << ": " << ripple << " dB RMS"
						   << std::string(inside ? "  <- inside the measured band" : ""));
	}
	CHECK(any_inside);

	// And the default is a gentle filter whatever else it is. An order of magnitude
	// deeper would be the metallic comb the measurement says does not exist.
	CHECK(defaulted > 0.8);
	CHECK(defaulted < 4.0);
}

TEST_CASE("a held rpm is not an exactly periodic waveform") {
	// The defect a spectrum cannot see. At a held rpm a harmonic stack repeats
	// exactly every 1/f0 seconds, and an exactly repeating waveform is a beep
	// rather than an engine — which is audible immediately on a static rev even
	// when every harmonic level is right. Combustion jitter is what breaks the
	// repetition, and this measures whether it actually does.
	const double sample_rate = 48000.0;
	const int period = 480; // 6,000 rpm, f0 100 Hz
	const int frames = period * 8;

	auto period_error = [&](double jitter) {
		EngineSynth synth;
		synth.configure(bare_stack(), sample_rate);
		synth.set_combustion_jitter(jitter);
		std::vector<float> samples;
		render_steady(synth, driving(6000.0, 1.0), samples, frames, sample_rate);
		double signal = 0.0;
		double difference = 0.0;
		for (int index = period; index < frames; ++index) {
			const double a = static_cast<double>(samples[index]);
			const double b = static_cast<double>(samples[index - period]);
			signal += a * a;
			difference += (a - b) * (a - b);
		}
		return std::sqrt(difference / signal);
	};

	const double rigid = period_error(0.0);
	const double alive = period_error(EngineSynth().combustion_jitter());
	MESSAGE("period-to-period difference, RMS relative: no jitter " << rigid
																   << ", default jitter " << alive);

	// With the jitter off the waveform repeats to within the sine table's own
	// floor. With it on, consecutive firings differ by a percent or more, which is
	// the difference between an engine and a test tone.
	CHECK(rigid < 0.01);
	CHECK(alive > 0.05);
	// And it is on by default, because a default that has to be switched on is a
	// default nobody switches on.
	CHECK(EngineSynth().combustion_jitter() > 0.0);
}

TEST_CASE("the same inputs give the same samples, bit for bit") {
	// ARCHITECTURE.md §8. Two synths, identical calls, identical bits — including
	// through both PCG32 streams, which is the part that would break first if
	// anything ever reached for a clock or a global generator.
	const double sample_rate = 44100.0;
	const int frames = 333; // deliberately not a divisor of anything

	EngineSynth first;
	EngineSynth second;
	first.configure(EngineAudioConfig(), sample_rate);
	second.configure(EngineAudioConfig(), sample_rate);

	std::vector<float> a(frames);
	std::vector<float> b(frames);
	bool identical = true;
	for (int tick = 0; tick < 200; ++tick) {
		EngineAudioInput input = driving(3000.0 + 60.0 * static_cast<double>(tick), 0.8);
		input.trailing = (tick % 7) == 0;
		input.shifting = (tick % 11) == 0;
		input.on_limiter = (tick % 13) == 0;
		input.over_rev = (tick % 17) == 0;
		input.clutch_slip = (tick % 5) == 0 ? 250.0 : 0.0;

		first.publish(input);
		first.render(a.data(), frames);
		second.publish(input);
		second.render(b.data(), frames);
		for (int index = 0; index < frames; ++index) {
			if (a[index] != b[index]) {
				identical = false;
			}
		}
	}
	CHECK(identical);

	// And a reset returns a synth to the state a fresh one is in.
	first.reset();
	EngineSynth fresh;
	fresh.configure(EngineAudioConfig(), sample_rate);
	bool reset_identical = true;
	for (int tick = 0; tick < 20; ++tick) {
		const EngineAudioInput input = driving(9000.0, 1.0);
		first.publish(input);
		first.render(a.data(), frames);
		fresh.publish(input);
		fresh.render(b.data(), frames);
		for (int index = 0; index < frames; ++index) {
			if (a[index] != b[index]) {
				reset_identical = false;
			}
		}
	}
	CHECK(reset_identical);
}

TEST_CASE("one second of audio costs what section 15 can afford") {
	// §15 gives audio 0.5 ms of a 16.6 ms frame, which is 3.01% of one core. A
	// second of audio therefore has 30.1 ms to spend. This prints the real number
	// so that budget can be checked against something rather than assumed.
	//
	// Two rpm because the cost is the partial count and the partial count is
	// 8 kHz / f0: at 12,000 rpm the stack is 40 partials and at 2,200 rpm it is
	// 192, the cap. The second figure is the worst case the synth can reach and it
	// only happens near idle.
	const double sample_rate = 48000.0;
	const int block = 512;
	const int blocks = static_cast<int>(sample_rate) / block;

	for (const double rpm : { 12000.0, 2200.0 }) {
		EngineSynth synth;
		synth.configure(EngineAudioConfig(), sample_rate);
		std::vector<float> out(block);
		synth.publish(driving(rpm, 1.0));
		for (int repeat = 0; repeat < 20; ++repeat) {
			synth.render(out.data(), block); // warm the caches, settle the smoothers
		}

		const auto start = std::chrono::steady_clock::now();
		for (int index = 0; index < blocks; ++index) {
			// One publish per 400 samples would be the real 120 Hz rate; publishing
			// per block is close enough and keeps the measurement about `render`.
			synth.publish(driving(rpm, 1.0));
			synth.render(out.data(), block);
		}
		const auto finish = std::chrono::steady_clock::now();
		const double microseconds =
				std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(finish - start).count();

		MESSAGE(rpm << " rpm, " << synth.partial_count() << " partials: "
					<< microseconds << " us for " << (blocks * block) << " samples ("
					<< (microseconds / 30100.0 * 100.0) << "% of section 15's 0.5 ms per frame)");
		// A very loose ceiling. The point of this test is the number it prints; the
		// assertion only exists to catch a regression of the kind that would put a
		// libm call back in the inner loop.
		CHECK(microseconds < 300000.0);
	}
}

TEST_CASE("a stopped engine is silent and an unconfigured render is safe") {
	EngineSynth synth;
	synth.configure(EngineAudioConfig(), 48000.0);

	std::vector<float> block(256, 1.0f);
	// rpm 0 with everything else asking for noise: the idle fade has to win.
	EngineAudioInput input;
	input.load = 1.0;
	input.throttle = 1.0;
	input.clutch_slip = 500.0;
	synth.publish(input);
	for (int repeat = 0; repeat < 100; ++repeat) {
		synth.render(block.data(), 256);
	}
	double peak = 0.0;
	for (const float sample : block) {
		const double magnitude = std::fabs(static_cast<double>(sample));
		if (magnitude > peak) {
			peak = magnitude;
		}
	}
	MESSAGE("peak at 0 rpm with full load and a slipping clutch: " << peak);
	CHECK(peak < 1e-9);

	// A null buffer and a zero-length block are no-ops rather than crashes: the
	// audio callback is the one place in this project where a defensive branch is
	// cheaper than the alternative.
	synth.render(nullptr, 256);
	synth.render(block.data(), 0);
	synth.render(block.data(), -1);
}
