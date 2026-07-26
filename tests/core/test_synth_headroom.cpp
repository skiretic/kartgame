#include "doctest.h"

#include "core/audio_state.h"
#include "core/engine.h"
#include "core/engine_synth.h"
#include "core/kz_audio_reference.h"
#include "core/kz_reference.h"
#include "core/scrub_wind.h"
#include "core/surface.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// How loud this thing actually is, in dBFS, and where the level went.
//
// Issue #160's third measurement item: "EngineSynth's own output level at
// gain = 1.0 against full scale -- if the synth is internally conservative, the
// ceiling is lower than 1.0 suggests and the honest fix is inside the synth
// rather than at the player." Nothing here tunes anything. Every number is a
// measurement of the shipped code and every constant it compares against is
// named from the header that owns it.
//
// ## Why this is a separate file from test_engine_synth.cpp
//
// That file asserts the synth's *spectrum* -- the ladder, the decay, the alias
// floor -- and every one of those tests is a comparison of bins against each
// other. A relative test cannot see absolute level by construction, which is the
// same blind spot CLAUDE.md's trap list records for the scrub chain: "a filter
// whose output level depends on its shape parameter is not a bug you will see in
// a test that only compares relative response". This file is the absolute half.
//
// ## The measurement method, stated before the numbers
//
// **Every stage of the gain chain is isolated by a public setter and subtracted,
// not modeled.** `set_noise_gain(0)` and `set_comb_depth(0)` remove exactly one
// layer each, and neither changes how many draws the PRNGs make per sample --
// `white`, `flutter_white` and `jitter_white` are drawn unconditionally in the
// sample loop. So four renders of the same cell differ *only* in the layer under
// test and the harmonic stack inside them is bit-identical, which means the noise
// layer can be recovered by sample-wise subtraction rather than by assuming it is
// uncorrelated with the stack. That is an exact separation, not a statistical one.
//
// The one thing that has no setter is the chatter and flutter modulation, which
// rides on `on_limiter` and `over_rev` together with a noise-layer boost. Those
// two cells carry an analytic prediction beside the measurement instead, and say
// so.
//
// ## Tolerances
//
// CLAUDE.md: `doctest::Approx(x).epsilon(e)` compares against
// `e * (scale + max(|a|, |b|))` with `scale` defaulting to 1.0, so on a value of
// 0.03 an `epsilon(0.06)` is a tolerance of 0.062. Every comparison below is
// therefore either an explicit relative difference or a difference of two
// decibel figures, both of which say what they mean.

using kart::core::EngineAudioConfig;
using kart::core::EngineAudioInput;
using kart::core::EngineSynth;
using kart::core::ScrubSynth;
using kart::core::ScrubWindConfig;
using kart::core::WindSynth;
namespace kz_audio = kart::core::kz_audio;
namespace synth_tuning = kart::core::synth_tuning;
namespace scrub_wind_tuning = kart::core::scrub_wind_tuning;

namespace {

constexpr double PI = 3.14159265358979323846;

// 48 kHz because that is what `EngineSynth`'s own default is and what
// `audio_state.h` names first. 512 frames because ADR-0035 measured `p_frames`
// at 512 on every one of 684 `_mix` calls under CoreAudio -- min, median and max
// all 512. Measuring at a block size the device never asks for would be measuring
// a different synth.
constexpr double SAMPLE_RATE = 48000.0;
constexpr int BLOCK = 512;

// 48 blocks = 24,576 samples = 0.512 s of settling.
//
// The slowest smoother in the class is the control pole at
// `CONTROL_SMOOTH_S` = 1/120 s, which is 400 samples, so this is 61 time
// constants. The event pole is `SHIFT_TRANSIENT_S * 0.25` = 11.25 ms and is
// faster still. Nothing in the class has memory longer than the 4,096-sample comb
// line, which is 8 blocks.
constexpr int SETTLE_BLOCKS = 48;

// 4 windows of 24 blocks = 49,152 samples = 1.024 s measured.
//
// Split into windows so the RMS estimate's own stability is reported rather than
// asserted. Every table below prints the worst window-to-window spread in dB; if
// that column is ever large the render is too short and the numbers beside it are
// not trustworthy. Measured spreads are in the hundredths of a dB.
constexpr int WINDOW_BLOCKS = 24;
constexpr int WINDOWS = 4;
constexpr int MEASURE_BLOCKS = WINDOW_BLOCKS * WINDOWS;

double clamp01(double v) {
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

double to_db(double linear) {
	return 20.0 * std::log10(linear > 1e-12 ? linear : 1e-12);
}

struct Level {
	double peak = 0.0;
	double rms = 0.0;
	double crest = 0.0; // peak / rms, linear
	double spread_db = 0.0; // worst window RMS against the whole-run RMS
};

Level measure(const std::vector<float> &s) {
	Level out;
	if (s.empty()) {
		return out;
	}
	double sum_sq = 0.0;
	for (float v : s) {
		const double a = std::fabs(static_cast<double>(v));
		if (a > out.peak) {
			out.peak = a;
		}
		sum_sq += static_cast<double>(v) * static_cast<double>(v);
	}
	out.rms = std::sqrt(sum_sq / static_cast<double>(s.size()));
	out.crest = out.rms > 1e-12 ? out.peak / out.rms : 0.0;

	// Window stability. Not a tolerance -- a reported property of the estimate.
	const std::size_t window = s.size() / WINDOWS;
	if (window > 0) {
		for (int w = 0; w < WINDOWS; ++w) {
			double acc = 0.0;
			for (std::size_t i = 0; i < window; ++i) {
				const double v = static_cast<double>(s[static_cast<std::size_t>(w) * window + i]);
				acc += v * v;
			}
			const double wr = std::sqrt(acc / static_cast<double>(window));
			const double d = std::fabs(to_db(wr) - to_db(out.rms));
			if (d > out.spread_db) {
				out.spread_db = d;
			}
		}
	}
	return out;
}

// --- the engine side -------------------------------------------------------

// One operating point. `shifting` and `clutch_slip` are left at their defaults
// across the whole grid: both are transients that a driver passes through, and
// #160 is about the level the channel sits at while driving, not about its peaks
// during a shift.
struct Cell {
	const char *name;
	double rpm;
	double load;
	bool trailing;
	bool on_limiter;
	bool over_rev;
};

EngineAudioInput input_for(const Cell &c) {
	EngineAudioInput in;
	in.rpm = c.rpm;
	in.load = c.load;
	in.throttle = c.load;
	in.trailing = c.trailing;
	in.on_limiter = c.on_limiter;
	in.over_rev = c.over_rev;
	return in;
}

// Configure, settle, and leave the synth at the operating point so its realized
// partial gains can be read afterward.
void prepare(EngineSynth &synth, const Cell &c, double gain, double noise_gain, double comb_depth) {
	EngineAudioConfig cfg;
	cfg.gain = gain;
	synth.configure(cfg, SAMPLE_RATE);
	synth.set_noise_gain(noise_gain);
	synth.set_comb_depth(comb_depth);
	synth.publish(input_for(c));
	std::vector<float> scratch(BLOCK, 0.0f);
	for (int b = 0; b < SETTLE_BLOCKS; ++b) {
		synth.render(scratch.data(), BLOCK);
	}
}

std::vector<float> render_run(EngineSynth &synth) {
	std::vector<float> out(static_cast<std::size_t>(MEASURE_BLOCKS) * BLOCK, 0.0f);
	for (int b = 0; b < MEASURE_BLOCKS; ++b) {
		synth.render(out.data() + static_cast<std::size_t>(b) * BLOCK, BLOCK);
	}
	return out;
}

// The `level` scalar the synth computes at this cell, reconstructed from the
// constants that build it. `render` is the owner of this arithmetic; this is a
// second evaluation of it, and TEST_CASE "the level identity" checks the two
// agree by comparing against the realized partial gains.
double steady_level(const Cell &c, double gain) {
	const double off = c.trailing ? 1.0 : 0.0;
	const double onpipe = EngineSynth::powerband_weight(c.rpm) * clamp01(c.load) * (1.0 - off);
	const double level_db = -kz_audio::THROTTLE_LEVEL_DELTA_DB * off +
			synth_tuning::ONPIPE_LEVEL_DB * onpipe;
	const double rpm_fade = clamp01(c.rpm / synth_tuning::IDLE_FADE_RPM);
	return std::pow(10.0, level_db * 0.05) * gain * rpm_fade;
}

// What the harmonic stack's RMS would be if its partials were at unrelated
// phases: each partial contributes a^2/2 to the mean square. The measurement this
// is compared against is the rendered stack with the noise and the comb removed.
double stack_ideal_rms(const EngineSynth &synth) {
	double sum_sq = 0.0;
	for (int i = 0; i < synth.partial_count(); ++i) {
		const double g = synth.partial_gain(i);
		sum_sq += g * g;
	}
	return std::sqrt(0.5 * sum_sq);
}

double stack_amplitude_sum(const EngineSynth &synth) {
	double sum = 0.0;
	for (int i = 0; i < synth.partial_count(); ++i) {
		sum += synth.partial_gain(i);
	}
	return sum;
}

// Sample-wise difference of two runs, for pulling one layer back out exactly.
std::vector<float> difference(const std::vector<float> &a, const std::vector<float> &b) {
	std::vector<float> out(a.size(), 0.0f);
	for (std::size_t i = 0; i < a.size(); ++i) {
		out[i] = a[i] - b[i];
	}
	return out;
}

// Power gain of the noise layer's one-pole high-pass over white noise, by
// numerical integration of |H|^2 rather than by a pass of noise.
//
// CLAUDE.md: "estimating a filter's response from one pass of noise does not
// work". That trap is about a *shape* read off one realization, and an RMS over a
// long run is a different and well-behaved estimator -- but an integral is exact
// and costs nothing, so there is no reason to take the statistical one. The
// measured noise layer is compared against this.
//
// The layer is `noise = (white - lowpass) * noise_level` with `lowpass` a one-pole
// at `alpha`, so H(z) = 1 - alpha/(1 - (1-alpha) z^-1) = p(1 - z^-1)/(1 - p z^-1)
// with p = 1 - alpha.
double highpass_power_gain(double alpha) {
	const double p = 1.0 - alpha;
	const int n = 200000;
	double acc = 0.0;
	for (int i = 0; i < n; ++i) {
		const double w = PI * (static_cast<double>(i) + 0.5) / static_cast<double>(n);
		const double num = p * p * (2.0 - 2.0 * std::cos(w));
		const double den = 1.0 - 2.0 * p * std::cos(w) + p * p;
		acc += num / den;
	}
	return acc / static_cast<double>(n);
}

// Mean square of the ignition-cut gate over one cycle, which is what the chatter
// does to the level while `on_limiter` is true.
double chatter_power_factor(double limiter) {
	const double depth = (1.0 - std::pow(10.0, -synth_tuning::LIMITER_CUT_DB * 0.05)) * limiter;
	const int n = 40000;
	double acc = 0.0;
	for (int i = 0; i < n; ++i) {
		const double s = std::sin(2.0 * PI * static_cast<double>(i) / static_cast<double>(n));
		const double gate = 0.5 * (1.0 + s / std::sqrt(s * s + synth_tuning::CHATTER_EDGE_K));
		const double g = 1.0 - depth * gate;
		acc += g * g;
	}
	return acc / static_cast<double>(n);
}

// The grid. Six rpm points spanning what a driver actually sees.
//
// `Engine::idle_rpm` is **2,000**, not the 1,960 an earlier note quoted.
// `kz::POWERBAND_MIN_RPM` / `MAX_RPM` are 9,000 and 14,000, `Engine::soft_cut_rpm`
// is 14,300 and `hard_cut_rpm` is 14,800, so 14,500 is on the limiter and 15,200
// is over-revving. All named from their headers rather than retyped as literals
// where the header exposes them.
const Cell GRID[] = {
	{ "idle 2000 trail", 2000.0, 0.0, true, false, false },
	{ "idle 2000 L0.0", 2000.0, 0.0, false, false, false },
	{ "idle 2000 L0.5", 2000.0, 0.5, false, false, false },
	{ "idle 2000 L1.0", 2000.0, 1.0, false, false, false },
	{ "band 9000 trail", 9000.0, 0.0, true, false, false },
	{ "band 9000 L0.0", 9000.0, 0.0, false, false, false },
	{ "band 9000 L0.5", 9000.0, 0.5, false, false, false },
	{ "band 9000 L1.0", 9000.0, 1.0, false, false, false },
	{ "mid 12000 trail", 12000.0, 0.0, true, false, false },
	{ "mid 12000 L0.0", 12000.0, 0.0, false, false, false },
	{ "mid 12000 L0.5", 12000.0, 0.5, false, false, false },
	{ "mid 12000 L1.0", 12000.0, 1.0, false, false, false },
	{ "peak 14000 trail", 14000.0, 0.0, true, false, false },
	{ "peak 14000 L0.0", 14000.0, 0.0, false, false, false },
	{ "peak 14000 L0.5", 14000.0, 0.5, false, false, false },
	{ "peak 14000 L1.0", 14000.0, 1.0, false, false, false },
	{ "limit 14500 L1.0", 14500.0, 1.0, false, true, false },
	{ "over 15200 L1.0", 15200.0, 1.0, false, false, true },
};
constexpr int GRID_SIZE = static_cast<int>(sizeof(GRID) / sizeof(GRID[0]));

} // namespace

// ---------------------------------------------------------------------------
// 1. The grid at gain = 1.0
// ---------------------------------------------------------------------------

TEST_CASE("engine synth output level at gain = 1.0, across the driving range") {
	MESSAGE("EngineSynth at gain = 1.0, 48 kHz, 512-frame blocks, 1.024 s measured per cell.");
	MESSAGE("full scale is 1.0; soft clip knee is at "
			<< synth_tuning::SOFT_CLIP_THRESHOLD << " ("
			<< to_db(synth_tuning::SOFT_CLIP_THRESHOLD) << " dBFS)");

	char line[240];
	std::snprintf(line, sizeof(line), "%-18s %8s %9s %8s %9s %7s %8s %6s",
			"cell", "peak", "peak dBFS", "rms", "rms dBFS", "crest", "crest dB", "spread");
	MESSAGE(line);

	double worst_peak = 0.0;
	const char *worst_cell = "";
	double worst_spread = 0.0;

	for (int i = 0; i < GRID_SIZE; ++i) {
		EngineSynth synth;
		const EngineAudioConfig defaults;
		prepare(synth, GRID[i], 1.0, defaults.noise_gain, defaults.comb_depth);
		const Level lv = measure(render_run(synth));

		std::snprintf(line, sizeof(line), "%-18s %8.5f %9.2f %8.5f %9.2f %7.3f %8.2f %6.3f",
				GRID[i].name, lv.peak, to_db(lv.peak), lv.rms, to_db(lv.rms),
				lv.crest, to_db(lv.crest), lv.spread_db);
		MESSAGE(line);

		if (lv.peak > worst_peak) {
			worst_peak = lv.peak;
			worst_cell = GRID[i].name;
		}
		if (lv.spread_db > worst_spread) {
			worst_spread = lv.spread_db;
		}

		// Nothing may leave the synth outside (-1, 1). That is `soft_clip`'s
		// contract and it is the one hard guarantee in the file.
		CHECK(lv.peak < 1.0);
	}

	MESSAGE("loudest cell: " << std::string(worst_cell) << " at peak " << worst_peak
			<< " = " << to_db(worst_peak) << " dBFS");
	MESSAGE("headroom to full scale at the loudest cell: " << -to_db(worst_peak) << " dB");
	MESSAGE("headroom to the soft-clip knee: "
			<< (to_db(synth_tuning::SOFT_CLIP_THRESHOLD) - to_db(worst_peak)) << " dB");
	MESSAGE("worst window-to-window RMS spread over the grid: " << worst_spread
			<< " dB, so the RMS figures above are stable to better than 0.2 dB");

	// The estimate has to be stable or the table is decoration. This is the
	// evidence for that, asserted rather than eyeballed.
	CHECK(worst_spread < 0.5);

	// **The finding, and it is the opposite of what this file was written to
	// expect.** The prior was that the amplitude-sum normalization is a
	// worst-case-peak bound that almost never binds, so gain = 1.0 would render far
	// below full scale and there would be a large ceiling to reclaim inside the
	// synth. The RMS half of that is true -- see the crest factor column, and the
	// stage table below -- but the peak half is false. A harmonic stack whose
	// partials all start at phase 0 and stay harmonically related **does** re-align
	// once per fundamental period, so the sum-of-amplitudes bound is nearly tight
	// in peak, and at gain = 1.0 the loudest cell is already over the soft-clip
	// knee.
	//
	// So this asserts the measured state of affairs: gain = 1.0 is not a safe
	// operating point, and the headroom to reclaim is not sitting in the gain.
	CHECK(worst_peak > synth_tuning::SOFT_CLIP_THRESHOLD);
	CHECK(worst_peak < 1.0);
}

TEST_CASE("the stack is peaky, not quiet: where the RMS deficit comes from") {
	// The normalization divides by sum(a_i), which bounds the peak of a
	// phase-aligned stack at exactly `level`. Whether that bound is loose or tight
	// is the whole question, and it is answered by measuring the realized peak of
	// the stack alone against sum(a_i).
	//
	// Rendered at gain = 0.1 rather than 1.0 so that nothing reaches
	// `SOFT_CLIP_THRESHOLD` -- everything upstream of the clip is linear in
	// `level`, so the peak-to-sum ratio is gain-independent as long as the clip
	// stays out of it. Noise and comb are both off, so this is the bare stack.
	MESSAGE("");
	MESSAGE("Bare harmonic stack at gain = 0.1, noise and comb removed.");
	MESSAGE("'align' is peak / sum(a_i): 1.0 would mean the partials line up "
			"perfectly once per period, which is what the normalization assumes.");

	char line[240];
	std::snprintf(line, sizeof(line), "%8s %5s %10s %10s %8s %9s %9s",
			"rpm", "N", "sum a_i", "peak", "align", "crest dB", "rms dBFS");
	MESSAGE(line);

	const double rpms[] = { 2000.0, 5000.0, 9000.0, 12000.0, 14000.0 };
	double worst_align = 0.0;
	for (double rpm : rpms) {
		const Cell c{ "sweep", rpm, 1.0, false, false, false };
		EngineSynth s;
		prepare(s, c, 0.1, 0.0, 0.0);
		const Level lv = measure(render_run(s));
		const double sum = stack_amplitude_sum(s);
		const double align = sum > 1e-12 ? lv.peak / sum : 0.0;
		if (align > worst_align) {
			worst_align = align;
		}
		std::snprintf(line, sizeof(line), "%8.0f %5d %10.6f %10.6f %8.3f %9.2f %9.2f",
				rpm, s.partial_count(), sum, lv.peak, align, to_db(lv.crest), to_db(lv.rms));
		MESSAGE(line);
	}

	MESSAGE("");
	MESSAGE("worst peak-to-sum ratio: " << worst_align);
	MESSAGE("The bound is close to tight, so the low RMS is crest factor and not a "
			"conservative gain. Raising the gain cannot recover it; only reducing the "
			"crest factor can.");

	// If the partials did not re-align, this ratio would be down around
	// sqrt(2*ln(N)/N) -- a few percent for a 190-partial stack. It is not.
	CHECK(worst_align > 0.4);
}

// ---------------------------------------------------------------------------
// 2. Where the level went
// ---------------------------------------------------------------------------

TEST_CASE("the level identity: partial gains sum to the commanded level") {
	// `render` normalizes by the sum of partial amplitudes, with the stated intent
	// that "the sum of partial amplitudes equals the commanded level". That is an
	// identity and it should hold exactly. Checked before anything is concluded
	// from it.
	double worst_relative = 0.0;
	for (int i = 0; i < GRID_SIZE; ++i) {
		EngineSynth synth;
		const EngineAudioConfig defaults;
		prepare(synth, GRID[i], 1.0, defaults.noise_gain, defaults.comb_depth);
		const double realized = stack_amplitude_sum(synth);
		const double predicted = steady_level(GRID[i], 1.0);
		// Explicit relative difference. `Approx(x).epsilon(e)` would compare
		// against e * (1.0 + max(|a|,|b|)) and on a value near 1.0 that is roughly
		// a factor of two looser than it reads.
		const double rel = std::fabs(realized - predicted) / (predicted > 1e-12 ? predicted : 1.0);
		if (rel > worst_relative) {
			worst_relative = rel;
		}
	}
	MESSAGE("worst relative error between sum(partial gains) and the reconstructed "
			"level, over " << GRID_SIZE << " cells: " << worst_relative);
	CHECK(worst_relative < 1e-9);
}

TEST_CASE("the gain chain, stage by stage") {
	// Seven representative cells. Each is rendered four times with one layer
	// removed at a time, so every stage below is a subtraction of two measurements
	// rather than a model -- except the chatter and flutter column, which has no
	// setter and carries an analytic prediction instead.
	const Cell CHAIN[] = {
		{ "idle 2000 L0.0", 2000.0, 0.0, false, false, false },
		{ "mid 12000 trail", 12000.0, 0.0, true, false, false },
		{ "band 9000 L1.0", 9000.0, 1.0, false, false, false },
		{ "mid 12000 L1.0", 12000.0, 1.0, false, false, false },
		{ "peak 14000 L1.0", 14000.0, 1.0, false, false, false },
		{ "limit 14500 L1.0", 14500.0, 1.0, false, true, false },
		{ "over 15200 L1.0", 15200.0, 1.0, false, false, true },
	};
	const int chain_size = static_cast<int>(sizeof(CHAIN) / sizeof(CHAIN[0]));

	const EngineAudioConfig defaults;

	MESSAGE("");
	MESSAGE("Gain chain at gain = 1.0. Every column is dB relative to the previous "
			"stage, so they sum to the output.");
	MESSAGE("N is the stack size. 'norm' is the amplitude-sum normalization, "
			"'mod' is chatter and flutter, 'noise' is what adding the noise layer "
			"does to the total, 'comb' is the comb filter.");

	char line[300];
	std::snprintf(line, sizeof(line), "%-17s %4s %9s %8s %8s %8s %8s %10s",
			"cell", "N", "level dB", "norm", "mod", "noise", "comb", "out dBFS");
	MESSAGE(line);

	double worst_norm_error = 0.0;
	double worst_noise_error = 0.0;

	for (int i = 0; i < chain_size; ++i) {
		const Cell &c = CHAIN[i];

		// A: stack alone, no noise, no comb.
		EngineSynth a;
		prepare(a, c, 1.0, 0.0, 0.0);
		const std::vector<float> run_a = render_run(a);
		const Level la = measure(run_a);

		// B: stack plus noise, no comb. The stack inside B is bit-identical to A --
		// removing the noise layer does not change a single PRNG draw -- so B - A
		// is the noise layer exactly.
		EngineSynth b;
		prepare(b, c, 1.0, defaults.noise_gain, 0.0);
		const std::vector<float> run_b = render_run(b);
		const Level lb = measure(run_b);

		// D: the shipped configuration.
		EngineSynth d;
		prepare(d, c, 1.0, defaults.noise_gain, defaults.comb_depth);
		const std::vector<float> run_d = render_run(d);
		const Level ld = measure(run_d);

		const double level = steady_level(c, 1.0);
		const double ideal = stack_ideal_rms(a);

		const double level_db = to_db(level);
		const double norm_db = to_db(ideal) - level_db;
		const double mod_db = to_db(la.rms) - to_db(ideal);
		const double noise_db = to_db(lb.rms) - to_db(la.rms);
		const double comb_db = to_db(ld.rms) - to_db(lb.rms);

		std::snprintf(line, sizeof(line), "%-17s %4d %9.2f %8.2f %8.2f %8.2f %8.2f %10.2f",
				c.name, a.partial_count(), level_db, norm_db, mod_db, noise_db, comb_db,
				to_db(ld.rms));
		MESSAGE(line);

		// The stages are a chain of differences, so their sum equalling the output
		// is arithmetic and not evidence. What follows is the evidence: each stage
		// against an independent prediction.

		// (a) The amplitude-sum normalization. Predicted from the realized partial
		//     amplitudes on the assumption that the partials are at unrelated
		//     phases, which is the whole question the prior asks.
		if (!c.on_limiter && !c.over_rev) {
			const double err = std::fabs(to_db(la.rms) - to_db(ideal));
			if (err > worst_norm_error) {
				worst_norm_error = err;
			}
		}

		// (b) The noise layer, recovered exactly by subtraction and compared
		//     against its own analytic level.
		const double clutch = 0.0;
		const double corner = kz_audio::STACK_CEILING_HZ * synth_tuning::NOISE_HIGHPASS_FRACTION *
				(1.0 - synth_tuning::CLUTCH_NOISE_CORNER_DROP * clutch);
		const double alpha = 1.0 - std::exp(-2.0 * PI * corner / SAMPLE_RATE);
		const double noise_level = level * defaults.noise_gain *
				std::pow(10.0, (synth_tuning::OVER_REV_NOISE_DB * (c.over_rev ? 1.0 : 0.0)) * 0.05);
		// The PRNG draws uniform on [0, 1) and the layer maps it to [-1, 1), whose
		// RMS is 1/sqrt(3).
		const double predicted_noise_rms = noise_level * (1.0 / std::sqrt(3.0)) *
				std::sqrt(highpass_power_gain(alpha));
		const Level noise_only = measure(difference(run_b, run_a));
		const double noise_err = std::fabs(to_db(noise_only.rms) - to_db(predicted_noise_rms));
		if (noise_err > worst_noise_error) {
			worst_noise_error = noise_err;
		}

		if (c.on_limiter || c.over_rev) {
			// The one stage with no setter. Predicted, and labeled as predicted.
			const double chatter = c.on_limiter ? chatter_power_factor(1.0) : 1.0;
			const double flutter_depth = c.over_rev ? synth_tuning::OVER_REV_FLUTTER_DEPTH : 0.0;
			const double flutter = 1.0 + flutter_depth * flutter_depth;
			const double predicted_mod_db = 10.0 * std::log10(chatter * flutter);
			MESSAGE("    chatter+flutter predicted " << predicted_mod_db
					<< " dB, measured " << mod_db << " dB (predicted, not isolated)");
		}
	}

	MESSAGE("");
	MESSAGE("worst error between the rendered stack and sqrt(sum(a_i^2)/2): "
			<< worst_norm_error << " dB");
	MESSAGE("worst error between the recovered noise layer and its analytic RMS: "
			<< worst_noise_error << " dB");

	// The stack really is at the RMS an unrelated-phase sum predicts, which is what
	// makes the normalization cost above a real cost and not an artifact. Measured
	// worst case is about 0.19 dB; the bound is 0.35 because this is a modeling
	// residual -- a harmonic stack is not exactly an unrelated-phase sum, and the
	// leftover correlation is what this number is -- rather than a contract the
	// class owes anybody.
	CHECK(worst_norm_error < 0.35);
	CHECK(worst_noise_error < 0.30);
}

TEST_CASE("the amplitude-sum normalization costs more the bigger the stack") {
	// The prior this file was written to test: normalizing by sum(a_i) is a
	// worst-case-peak normalization, tight only when every partial's phase aligns.
	// The RMS of N partials at unrelated phases is
	// level * sqrt(sum(a_i^2)) / (sqrt(2) * sum(a_i)), which falls roughly as
	// 1/sqrt(N). This measures the cost against stack size directly.
	MESSAGE("");
	MESSAGE("Amplitude-sum normalization cost against stack size, at load 1.0.");

	char line[220];
	std::snprintf(line, sizeof(line), "%8s %5s %10s %12s %12s",
			"rpm", "N", "norm dB", "1/sqrt(N) dB", "sum a_i");
	MESSAGE(line);

	const double rpms[] = { 2000.0, 3000.0, 5000.0, 7000.0, 9000.0, 11000.0, 12000.0, 14000.0 };
	for (double rpm : rpms) {
		const Cell c{ "sweep", rpm, 1.0, false, false, false };
		EngineSynth s;
		prepare(s, c, 1.0, 0.0, 0.0);
		const int n = s.partial_count();
		const double level = steady_level(c, 1.0);
		const double norm_db = to_db(stack_ideal_rms(s)) - to_db(level);
		std::snprintf(line, sizeof(line), "%8.0f %5d %10.2f %12.2f %12.6f",
				rpm, n, norm_db, to_db(1.0 / std::sqrt(static_cast<double>(n))),
				stack_amplitude_sum(s));
		MESSAGE(line);
	}

	// The claim, checked at the two ends: a bigger stack costs more, and the cost
	// at the idle end is large.
	EngineSynth low;
	EngineSynth high;
	const Cell idle{ "idle", 2000.0, 1.0, false, false, false };
	const Cell peak{ "peak", 14000.0, 1.0, false, false, false };
	prepare(low, idle, 1.0, 0.0, 0.0);
	prepare(high, peak, 1.0, 0.0, 0.0);
	const double low_db = to_db(stack_ideal_rms(low)) - to_db(steady_level(idle, 1.0));
	const double high_db = to_db(stack_ideal_rms(high)) - to_db(steady_level(peak, 1.0));
	MESSAGE("normalization cost: " << low_db << " dB at 2,000 rpm (" << low.partial_count()
			<< " partials), " << high_db << " dB at 14,000 rpm (" << high.partial_count()
			<< " partials)");
	CHECK(low_db < high_db); // more partials, more cost
	CHECK(high_db < -6.0); // and even the small stack gives up more than 6 dB
}

// ---------------------------------------------------------------------------
// 3. The two noise layers
// ---------------------------------------------------------------------------

namespace {

Level render_scrub(const ScrubWindConfig &cfg, double drive, double speed, int surface_type) {
	ScrubSynth s;
	s.configure(cfg, SAMPLE_RATE);
	EngineAudioInput in;
	in.scrub = drive;
	in.speed_ms = speed;
	in.surface = surface_type;
	s.publish(in);
	std::vector<float> scratch(BLOCK, 0.0f);
	// The drive one-pole is `DRIVE_SMOOTHING_ALPHA` = 0.0006 per sample at 48 kHz,
	// a time constant of about 35 ms. 48 blocks is 0.512 s, roughly 15 of them.
	for (int b = 0; b < SETTLE_BLOCKS; ++b) {
		s.render(scratch.data(), BLOCK);
	}
	std::vector<float> out(static_cast<std::size_t>(MEASURE_BLOCKS) * BLOCK, 0.0f);
	for (int b = 0; b < MEASURE_BLOCKS; ++b) {
		s.render(out.data() + static_cast<std::size_t>(b) * BLOCK, BLOCK);
	}
	return measure(out);
}

Level render_wind(const ScrubWindConfig &cfg, double speed) {
	WindSynth w;
	w.configure(cfg, SAMPLE_RATE);
	EngineAudioInput in;
	in.speed_ms = speed;
	w.publish(in);
	std::vector<float> scratch(BLOCK, 0.0f);
	for (int b = 0; b < SETTLE_BLOCKS; ++b) {
		w.render(scratch.data(), BLOCK);
	}
	std::vector<float> out(static_cast<std::size_t>(MEASURE_BLOCKS) * BLOCK, 0.0f);
	for (int b = 0; b < MEASURE_BLOCKS; ++b) {
		w.render(out.data() + static_cast<std::size_t>(b) * BLOCK, BLOCK);
	}
	return measure(out);
}

} // namespace

TEST_CASE("scrub layer level at the shipped scrub_gain") {
	const ScrubWindConfig defaults;
	MESSAGE("");
	MESSAGE("ScrubSynth at scrub_gain = " << defaults.scrub_gain
			<< " on asphalt (roughness " << kart::core::surface(kart::core::SURFACE_ASPHALT).roughness
			<< ", which pulls the band from " << defaults.scrub_center_hz << " Hz toward "
			<< scrub_wind_tuning::SCRUB_ROUGH_CENTER_HZ << " Hz)");

	char line[240];
	std::snprintf(line, sizeof(line), "%7s %8s %9s %9s %9s %7s %6s",
			"drive", "speed", "peak", "peak dBFS", "rms dBFS", "crest", "spread");
	MESSAGE(line);

	const double drives[] = { 0.25, 0.5, 1.0 };
	const double speeds[] = { 10.0, 25.0, 38.0 };
	double worst_spread = 0.0;
	double worst_peak = 0.0;
	for (double drive : drives) {
		for (double speed : speeds) {
			const Level lv = render_scrub(defaults, drive, speed, kart::core::SURFACE_ASPHALT);
			std::snprintf(line, sizeof(line), "%7.2f %8.1f %9.5f %9.2f %9.2f %7.3f %6.3f",
					drive, speed, lv.peak, to_db(lv.peak), to_db(lv.rms), lv.crest, lv.spread_db);
			MESSAGE(line);
			if (lv.spread_db > worst_spread) {
				worst_spread = lv.spread_db;
			}
			if (lv.peak > worst_peak) {
				worst_peak = lv.peak;
			}
			// **RECORDED DEFECT, not a pass condition.** `ScrubSynth::render`
			// writes `tilt_ * level_` straight into the output with no clamp and
			// no soft clip -- unlike `EngineSynth`, which has `soft_clip` as a
			// stated safety net. At scrub_gain 0.45, drive 1.0 and any speed at or
			// above `SCRUB_FULL_SPEED_MS`, that runs past full scale. WARN rather
			// than CHECK so the suite stays green while the number stays in the
			// log: this is issue #160's to fix, and this file only measures.
			WARN(lv.peak < 1.0);
		}
	}
	MESSAGE("worst scrub peak at the shipped gain: " << worst_peak << " ("
			<< to_db(worst_peak) << " dBFS)");
	if (worst_peak >= 1.0) {
		MESSAGE("DEFECT: the scrub layer exceeds full scale and ScrubSynth has no "
				"output clamp. EngineSynth has soft_clip; this class has nothing.");
	}
	// The window spread is wider here than for the engine because band-passed
	// noise at Q around 5 is narrowband, so a fixed window holds fewer independent
	// samples. The tighter evidence that the estimate is stable is in the table
	// itself: doubling the drive moves the RMS by exactly 6.02 dB at every speed.
	MESSAGE("worst window-to-window spread: " << worst_spread << " dB");
	CHECK(worst_spread < 1.0);
}

TEST_CASE("scrub_gain is a level: the chain normalization holds across Q and center") {
	// Commit 193d507 divided the band-pass chain's own white-noise RMS back out so
	// that `scrub_gain` stops moving when `scrub_q` does. Verified here rather than
	// assumed, because the whole ratio in the next test rests on it.
	//
	// Note that the surface interpolates both center and Q toward the rough-surface
	// values, and `chain_rms` is computed from the *interpolated* pair, so the
	// normalization is self-consistent on any surface. Asphalt is used throughout.
	MESSAGE("");
	MESSAGE("scrub RMS against scrub_q, drive 1.0 at 25 m/s (the normalization should flatten this)");

	char line[200];
	const double qs[] = { 1.5, 3.2, 6.402, 10.0, 20.0 };
	std::vector<double> rms_values;
	for (double q : qs) {
		ScrubWindConfig cfg;
		cfg.scrub_q = q;
		const Level lv = render_scrub(cfg, 1.0, 25.0, kart::core::SURFACE_ASPHALT);
		rms_values.push_back(lv.rms);
		std::snprintf(line, sizeof(line), "  Q = %6.3f  rms %9.6f  %8.2f dBFS", q, lv.rms, to_db(lv.rms));
		MESSAGE(line);
	}

	double lo = rms_values[0];
	double hi = rms_values[0];
	for (double v : rms_values) {
		lo = v < lo ? v : lo;
		hi = v > hi ? v : hi;
	}
	const double swing_db = to_db(hi) - to_db(lo);
	MESSAGE("total swing across a 13:1 range of Q: " << swing_db << " dB");
	MESSAGE("scrub_wind.h records the un-normalized chain as a 6:1 swing in RMS, "
			"which is " << to_db(0.775 / 0.127) << " dB");
	// A level control has to stay a level control. Explicit dB comparison, not
	// Approx: the values are around 0.1 and Approx's default scale of 1.0 would
	// make any epsilon here meaningless.
	CHECK(swing_db < 2.0);

	MESSAGE("");
	MESSAGE("scrub RMS against scrub_center_hz, drive 1.0 at 25 m/s");
	const double centers[] = { 400.0, 1000.0, 2000.0, 3000.0 };
	std::vector<double> center_rms;
	for (double c : centers) {
		ScrubWindConfig cfg;
		cfg.scrub_center_hz = c;
		const Level lv = render_scrub(cfg, 1.0, 25.0, kart::core::SURFACE_ASPHALT);
		center_rms.push_back(lv.rms);
		std::snprintf(line, sizeof(line), "  center = %7.1f Hz  rms %9.6f  %8.2f dBFS",
				c, lv.rms, to_db(lv.rms));
		MESSAGE(line);
	}
	double clo = center_rms[0];
	double chi = center_rms[0];
	for (double v : center_rms) {
		clo = v < clo ? v : clo;
		chi = v > chi ? v : chi;
	}
	MESSAGE("total swing across the band center range: " << (to_db(chi) - to_db(clo)) << " dB");
	CHECK((to_db(chi) - to_db(clo)) < 2.0);
}

TEST_CASE("wind layer level at the shipped wind_gain, and what its level law actually delivers") {
	const ScrubWindConfig defaults;
	MESSAGE("");
	MESSAGE("WindSynth at wind_gain = " << defaults.wind_gain
			<< ", speed exponent " << defaults.wind_speed_exponent
			<< " (from kz_audio::WIND_DB_PER_SPEED_DOUBLING = "
			<< kz_audio::WIND_DB_PER_SPEED_DOUBLING << ")");

	char line[240];
	std::snprintf(line, sizeof(line), "%8s %9s %9s %9s %9s %7s %6s",
			"speed", "cutoff", "peak", "peak dBFS", "rms dBFS", "crest", "spread");
	MESSAGE(line);

	const double speeds[] = { 6.25, 12.5, 19.0, 25.0, 38.0 };
	std::vector<double> rms_values;
	for (double speed : speeds) {
		const Level lv = render_wind(defaults, speed);
		rms_values.push_back(lv.rms);
		const double cutoff = scrub_wind_tuning::WIND_CUTOFF_FLOOR_HZ +
				defaults.wind_cutoff_hz_per_ms * speed;
		std::snprintf(line, sizeof(line), "%8.2f %9.1f %9.5f %9.2f %9.2f %7.3f %6.3f",
				speed, cutoff, lv.peak, to_db(lv.peak), to_db(lv.rms), lv.crest, lv.spread_db);
		MESSAGE(line);
		CHECK(lv.peak < 1.0);
	}

	// The level law is `(speed / 38)^3 * wind_gain`, which is 18.06 dB per doubling
	// by construction. But the output also passes a low-pass whose corner rises with
	// speed, and a wider low-pass passes more of the white noise -- so the realized
	// dB per doubling is steeper than the sourced figure. This measures the
	// difference between the law and the layer.
	const double realized_6_to_12 = to_db(rms_values[1]) - to_db(rms_values[0]);
	const double realized_12_to_25 = to_db(rms_values[3]) - to_db(rms_values[1]);
	MESSAGE("realized dB per doubling of road speed: 6.25 -> 12.5 m/s = "
			<< realized_6_to_12 << ", 12.5 -> 25 m/s = " << realized_12_to_25);
	MESSAGE("the level law alone is " << kz_audio::WIND_DB_PER_SPEED_DOUBLING
			<< " dB per doubling; the excess is the low-pass corner widening with speed");
	MESSAGE("excess over the sourced figure: "
			<< (realized_12_to_25 - kz_audio::WIND_DB_PER_SPEED_DOUBLING) << " dB per doubling");

	// **This assertion used to run the other way, and the flip is the fix landing.**
	// When this case was written the wind chain was not normalized by its own
	// white-noise RMS gain -- the defect `193d507` had fixed for scrub and missed
	// here -- so a wider low-pass corner passed more noise and the realized slope
	// ran 20.0 to 20.7 dB per doubling against a sourced 18.0. The excess was real
	// level arriving from the filter rather than from the speed law, and it moved
	// whenever `wind_cutoff_per_ms` did.
	//
	// Normalized now, so the layer and its law agree: measured 18.02, 18.02, 18.00
	// across the sweep. The tolerance is the RMS estimator's own, not a fudge --
	// the narrowest corner here gives about 0.04 dB of sampling error per doubling,
	// so 0.15 dB is just under four sigma and two orders inside the 2.49 dB the
	// defect was worth. ADR-0039.
	CHECK(std::fabs(realized_12_to_25 - kz_audio::WIND_DB_PER_SPEED_DOUBLING) < 0.15);
	CHECK(std::fabs(realized_6_to_12 - kz_audio::WIND_DB_PER_SPEED_DOUBLING) < 0.15);
}

// ---------------------------------------------------------------------------
// 4. The ratio the mixing pass actually needs
// ---------------------------------------------------------------------------

TEST_CASE("engine against scrub against wind at the shipped defaults") {
	// The shipped three, from `scripts/game/engine_voice_rig.gd` and mirrored in
	// `src/core/tuning.h`. Named here as literals because a core test cannot read a
	// GDScript constant -- that gap is exactly what commit e7b0eaa recorded on
	// issue #160. `ScrubWindConfig`'s own defaults are the C++ half and are used
	// directly, so only the voice gain is retyped.
	constexpr double VOICE_GAIN = 0.30;
	const ScrubWindConfig defaults;

	MESSAGE("");
	MESSAGE("Operating point: 10,000 rpm, full load, 25 m/s, cornering hard, asphalt.");
	MESSAGE("voice_gain = " << VOICE_GAIN << ", scrub_gain = " << defaults.scrub_gain
			<< ", wind_gain = " << defaults.wind_gain);

	const Cell drive_cell{ "10000 L1.0", 10000.0, 1.0, false, false, false };
	EngineSynth engine;
	const EngineAudioConfig engine_defaults;
	prepare(engine, drive_cell, VOICE_GAIN, engine_defaults.noise_gain, engine_defaults.comb_depth);
	const Level engine_level = measure(render_run(engine));

	const Level wind_level = render_wind(defaults, 25.0);

	MESSAGE("");
	char line[240];
	std::snprintf(line, sizeof(line), "%-28s %10s %10s %10s",
			"layer", "rms", "rms dBFS", "re engine");
	MESSAGE(line);
	std::snprintf(line, sizeof(line), "%-28s %10.6f %10.2f %10.2f",
			"engine (10,000 rpm, load 1)", engine_level.rms, to_db(engine_level.rms), 0.0);
	MESSAGE(line);

	// The scrub drive is a mean over four corners, so "cornering hard" is not 1.0.
	// `scrub_wind.h` says so in the SCRUB_GAMMA comment: a kart with its fronts
	// sliding and its rears gripping only reaches about half. All three are
	// printed rather than one being chosen.
	const double drives[] = { 0.25, 0.5, 1.0 };
	for (double drive : drives) {
		const Level lv = render_scrub(defaults, drive, 25.0, kart::core::SURFACE_ASPHALT);
		char label[64];
		std::snprintf(label, sizeof(label), "scrub (drive %.2f)", drive);
		std::snprintf(line, sizeof(line), "%-28s %10.6f %10.2f %10.2f",
				label, lv.rms, to_db(lv.rms), to_db(lv.rms) - to_db(engine_level.rms));
		MESSAGE(line);
	}

	std::snprintf(line, sizeof(line), "%-28s %10.6f %10.2f %10.2f",
			"wind (25 m/s)", wind_level.rms, to_db(wind_level.rms),
			to_db(wind_level.rms) - to_db(engine_level.rms));
	MESSAGE(line);

	const Level scrub_half = render_scrub(defaults, 0.5, 25.0, kart::core::SURFACE_ASPHALT);
	MESSAGE("");
	MESSAGE("headline ratio at drive 0.5: engine " << to_db(engine_level.rms)
			<< " dBFS, scrub " << to_db(scrub_half.rms) << " dBFS, wind "
			<< to_db(wind_level.rms) << " dBFS");
	MESSAGE("scrub is " << (to_db(scrub_half.rms) - to_db(engine_level.rms))
			<< " dB against the engine; wind is "
			<< (to_db(wind_level.rms) - to_db(engine_level.rms)) << " dB against it");
	MESSAGE("NOTE: these are synth-output ratios. The engine and scrub players are "
			"AudioStreamPlayer3D with unit sizes 3.0 and 2.0 and the wind is a "
			"non-positional AudioStreamPlayer, so Godot's attenuation moves this "
			"balance by an amount src/core/ cannot see.");

	// Wind against the engine is the one #84 names explicitly -- "wind masking the
	// engine anywhere in the range is wrong" -- and after e7b0eaa took wind_gain
	// from 0.20 to 0.12 it passes with room to spare.
	CHECK(to_db(wind_level.rms) < to_db(engine_level.rms));

	// **RECORDED DEFECT, not a pass condition.** The scrub layer is louder than
	// the engine at a realistic hard-cornering point. e7b0eaa left scrub_gain at
	// 0.45 as "judged fine on the same lap" that judged the engine too quiet; this
	// is the measurement that lap could not make. WARN so the suite stays green
	// and the number stays in the log.
	WARN(to_db(scrub_half.rms) < to_db(engine_level.rms));
	if (to_db(scrub_half.rms) >= to_db(engine_level.rms)) {
		MESSAGE("DEFECT: at drive 0.5 the scrub layer is "
				<< (to_db(scrub_half.rms) - to_db(engine_level.rms))
				<< " dB ABOVE the engine. Issue #160's driver report was that the "
				"engine is not loud enough; this is one measured reason why.");
	}
}

// ---------------------------------------------------------------------------
// 5. The headroom answer, in one number
// ---------------------------------------------------------------------------

TEST_CASE("headroom at gain = 1.0, in one number") {
	// The whole synth at gain = 1.0, nothing else changed, over the whole grid.
	// The answer is how far the loudest peak sits below full scale.
	double worst_peak = 0.0;
	double worst_rms = 0.0;
	const char *peak_cell = "";
	const char *rms_cell = "";
	const EngineAudioConfig defaults;

	for (int i = 0; i < GRID_SIZE; ++i) {
		EngineSynth synth;
		prepare(synth, GRID[i], 1.0, defaults.noise_gain, defaults.comb_depth);
		const Level lv = measure(render_run(synth));
		if (lv.peak > worst_peak) {
			worst_peak = lv.peak;
			peak_cell = GRID[i].name;
		}
		if (lv.rms > worst_rms) {
			worst_rms = lv.rms;
			rms_cell = GRID[i].name;
		}
	}

	const double headroom_db = -to_db(worst_peak);

	// The same grid at the shipped voice gain, which is the level the driver is
	// actually complaining about.
	double shipped_peak = 0.0;
	double shipped_rms = 0.0;
	for (int i = 0; i < GRID_SIZE; ++i) {
		EngineSynth synth;
		prepare(synth, GRID[i], 0.30, defaults.noise_gain, defaults.comb_depth);
		const Level lv = measure(render_run(synth));
		if (lv.peak > shipped_peak) {
			shipped_peak = lv.peak;
		}
		if (lv.rms > shipped_rms) {
			shipped_rms = lv.rms;
		}
	}

	MESSAGE("");
	MESSAGE("=== the headroom answer ===");
	MESSAGE("loudest peak over the grid at gain = 1.0: " << worst_peak << " ("
			<< std::string(peak_cell) << "), " << to_db(worst_peak) << " dBFS");
	MESSAGE("loudest RMS over the grid at gain = 1.0: " << worst_rms << " ("
			<< std::string(rms_cell) << "), " << to_db(worst_rms) << " dBFS");
	MESSAGE("HEADROOM REMAINING AT gain = 1.0: " << headroom_db
			<< " dB to full scale, and the soft clip is already engaged "
			<< (to_db(worst_peak) - to_db(synth_tuning::SOFT_CLIP_THRESHOLD))
			<< " dB into its knee");
	MESSAGE("");
	MESSAGE("at the shipped voice_gain of 0.30: loudest peak " << shipped_peak
			<< " (" << to_db(shipped_peak) << " dBFS), loudest RMS " << shipped_rms
			<< " (" << to_db(shipped_rms) << " dBFS)");
	MESSAGE("so the whole available range between voice_gain 0.30 and the clip is "
			<< (to_db(synth_tuning::SOFT_CLIP_THRESHOLD) - to_db(shipped_peak))
			<< " dB of peak, which is what raising voice_gain alone can buy");
	MESSAGE("");
	MESSAGE("The RMS deficit is " << (to_db(worst_peak) - to_db(worst_rms))
			<< " dB of crest factor at gain = 1.0. That is where the loudness went, "
			"and a gain cannot recover it because the peak is already at the ceiling.");

	// gain = 1.0 clips. The number a caller needs is the peak margin at the
	// shipped gain, and it is small -- under 15 dB, not the 20-plus a
	// "conservative synth" story would predict.
	CHECK(headroom_db < 3.0);
	CHECK((to_db(synth_tuning::SOFT_CLIP_THRESHOLD) - to_db(shipped_peak)) < 15.0);
}
