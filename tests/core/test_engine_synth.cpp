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
