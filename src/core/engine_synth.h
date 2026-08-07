#ifndef KART_CORE_ENGINE_SYNTH_H
#define KART_CORE_ENGINE_SYNTH_H

#include "core/audio_state.h"
#include "core/kz_audio_reference.h"
#include "core/kz_reference.h"
#include "core/pcg32.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

// The engine note. ARCHITECTURE.md §12: "harmonic stack with fundamental driven
// by RPM, per-harmonic gain envelopes shaped by load, noise layer, comb-filtered
// exhaust resonance."
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017 —
// which is what lets this file be unit tested against its own spectrum rather
// than judged by listening to it. `tests/core/test_engine_synth.cpp` does exactly
// that: it takes a DFT of the rendered samples and compares bin magnitudes
// against `kz_audio::PIPE_LADDER_DB`.
//
// ## State in, samples out, and nothing else
//
// This class owns no thread, no device, no buffer it did not size itself, and no
// Godot type. `publish` takes one tick of `EngineAudioInput`; `render` writes
// `frames` mono samples. Everything between is arithmetic over members allocated
// at `configure` time: no `new`, no `std::vector`, no lock, no exception, no
// syscall, nothing that can block. That is not a style preference — it is the
// only shape that is safe to call from an audio callback, and it is the reason
// this class can be dropped behind either of the two architectures issue #81 is
// still choosing between.
//
// **`publish` and `render` are assumed never to run concurrently.** This class
// does no synchronization of its own and no atomic publish: `publish` writes
// `input_` and `render` reads it. **Which transport makes that assumption true on
// Godot 4.7.1 is ADR-0035's problem, not this file's** — a ring buffer filled
// from the physics thread and drained by `AudioStreamGenerator::push_buffer`, or
// a custom `AudioStreamPlayback::_mix` with a single-producer seqlock in front of
// it, are two different answers and both leave this class unchanged. `configure`
// and `reset` are heavier than `render` (they touch the whole sine table) and are
// setup calls, not audio-thread calls.
//
// ## The two clocks
//
// `audio_state.h` states the rate mismatch at length and it drives most of what
// follows: the solver publishes at 120 Hz, the device asks for blocks of whatever
// size it likes, and neither clock knows about the other. At 48 kHz a tick is 400
// samples and a block might be 64 or 1024, so **this class never divides by the
// block length to build a ramp.** Every control is smoothed by a one-pole with a
// time constant in seconds, applied per sample; the block length only decides how
// many times the pole is applied. That is what makes the frequency continuous
// across a block boundary regardless of what the device does, which is #82's
// first acceptance criterion — "no audible stepping anywhere in the range".
//
// ## Load is a tilt, not a volume — and which columns that fits
//
// `REFERENCES.md`'s throttle table has three usable rows and they do not agree,
// which is the whole reason §12's "per-harmonic gain envelopes shaped by load" is
// the right shape of model and a single load scalar is not. What is implemented
// here is a **level** and a **tilt**, and it is worth being exact about what each
// half reproduces:
//
//   * `kz_audio::THROTTLE_LEVEL_DELTA_DB` = 4.45 dB is the level, and it **fits
//     the Colibri (+4.6 dB) and the D-7 (+4.3 dB)** — the two museum captures at a
//     fixed short distance from a stationary engine. It **does not fit the
//     Yamaha's +1.1 dB** and is not meant to: that recording is a bike riding away
//     from the microphone and its level change is contaminated by distance.
//
//   * `kz_audio::OFF_THROTTLE_TILT_DB_PER_DOUBLING` = 1.26 dB per doubling is the
//     shape, and it **fits the Yamaha's tilt column** — fundamental down 10.1 dB
//     against its local floor, h20-h24 down only 4-5, which is the measurement's
//     actual finding: off-throttle the note is relatively **brighter and thinner**,
//     not simply quieter. It **does not fit the D-7's tilt**, which runs the other
//     way at the low end (h2 and h3 are 3 dB stronger on throttle while h5-h8 are
//     3-4 dB weaker). No single monotonic tilt can fit both, that is stated in
//     `OFF_THROTTLE_TILT_DB_PER_DOUBLING`'s own comment, and this implementation
//     picks the Yamaha's sign because it is the one measured across a wide rev
//     range rather than at a single operating point.
//
// So the model fits two of the three columns for level and one of two for shape,
// and the D-7's low-harmonic reversal is not represented at all.
//
// `EngineAudioInput::load` deliberately does **not** scale the level. The measured
// split is two-state — frames were sorted by the sign of df0/dt and nothing in the
// corpus describes an in-between — so applying it as a continuous gain would be
// using a measurement for something other than what it measured. `load` enters
// through the on-pipe axis instead, which is where a driver actually hears part
// throttle on a two-stroke.
//
// ## What is deliberately not here
//
// Tire scrub and wind. §12 specifies both, and `EngineAudioInput` carries `scrub`
// and `speed_ms` for them, and **both fields are read by nothing in this file.**
// They are read by `core/scrub_wind.h`, which is a separate synthesizer feeding
// separate players — scrub positional on the kart, wind non-positional at the
// driver's head — because the two layers have to be separable at the mixer and a
// class rendering all three into one buffer could not be.
//
// **This paragraph used to be a refusal and it is now a division of labor**, which
// is worth stating because the refusal is the more instructive half. For two
// milestones both spectrum flags in `kz_audio_reference.h` were false, nothing in
// this project's corpus isolated either layer, and building them here would have
// meant inventing a filter shape — §5 item 10 is the rule that says do not, and a
// layer with a made-up spectrum is indistinguishable from a measured one six
// months later. Issue #84's sourcing pass then went and measured one. The assert
// below fired when that flag flipped, which is exactly what it was for.
static_assert(kart::core::kz_audio::SCRUB_SPECTRUM_MEASURED &&
				!kart::core::kz_audio::SCRUB_MEASURED_ON_KART_TIRE,
		"the scrub sourcing state changed: core/scrub_wind.h derives its band from "
		"kz_audio::SCRUB_PEAK_HZ and SCRUB_WIDTH_OCT, and core/tuning.h classifies "
		"those rows Derived specifically because the measurement is on a passenger "
		"car radial. If a kart tire was measured, both of those have to change and "
		"this assert exists so that flipping the flag brings you here");

namespace kart::core {

// The tunables. Everything here is a knob, not a measurement, and each one says
// what it is doing and why nothing measured could set it. They are grouped in
// their own namespace so that a reader can see at a glance how much of this
// synthesizer is sourced (the ladders, the throttle split, the comb delay, all in
// `kz_audio`) and how much is chosen (this list).
namespace synth_tuning {

// How far below the stack ceiling a partial starts fading out, as a fraction of
// the ceiling.
//
// The point of a fade rather than a switch: a partial that appears at full gain
// when rpm drops is a click, and #82's first criterion forbids exactly that. 0.15
// puts the fade band at 6.8-8.0 kHz with the default ceiling, which at a KZ's
// 14,000 rpm (f0 = 233 Hz) is about five partials wide — wide enough that the
// stack gains and loses partials continuously as rpm moves.
inline constexpr double CEILING_FADE_FRACTION = 0.15;

// Where the stack ends, as a **harmonic count** rather than a frequency.
//
// **This is the fix for "not enough two-stroke scream", and it is the one change
// in this file that a measurement forced rather than suggested.** The synth used
// to fill up to a fixed 8 kHz at every rpm. A ladder indexed by harmonic number
// under a ceiling fixed in Hz produces a spectrum whose center of gravity barely
// moves: measured on the shipped build, the spectral centroid went 3443 Hz at
// 6,000 rpm to 4046 at 13,000, an exponent of k = 0.21 in centroid ~ f0^k, and
// **inside the powerband it went backwards** — 4418 Hz at 9,000 down to 3770 at
// 14,500, k = -0.33. `kz_audio::CENTROID_EXPONENT_*` puts three racing two-strokes
// at k = 0.49 to 1.27 and the two muffled negative controls at 0.09 and 0.15. The
// shipped note was therefore measurably closer to a chainsaw than to a two-stroke,
// and over the range a driver actually uses it was on the wrong side of the
// chainsaw.
//
// A ceiling that is a harmonic count makes the stack's shape identical in harmonic
// space at every rpm, so the whole spectrum slides up bodily with f0 and k = 1.0
// by construction — inside the measured band, and near the middle of it in log
// terms. It is **derived** and not a free knob: k = 1 is what an index-indexed
// ladder gives, and the corpus brackets k = 1.
//
// 36 rather than 24 because 36 * f0 at 13,000 rpm is 7.8 kHz, which is where the
// old fixed ceiling put the top of the stack — so the change moves the note's
// brightness *at peak revs* by almost nothing and takes everything below peak
// revs down with rpm, which is the direction the measurement asked for. It also
// costs less: the stack was 160 partials at 3,000 rpm and is now 40.
inline constexpr double STACK_CEILING_HARMONICS = 36.0;

// The floor under `STACK_CEILING_HARMONICS * f0`, Hz.
//
// **A tunable.** At 2,000 rpm the harmonic ceiling alone is 1.2 kHz, and while the
// corpus's trackside kart recording really is that dark at low revs (centroid
// 530 Hz), it is dark partly because it is forty meters away. This keeps a
// stationary idling engine from losing its top two octaves to a microphone
// distance nobody measured. 2 kHz is one octave above where the harmonic ceiling
// crosses it, at 3,333 rpm.
inline constexpr double STACK_CEILING_MIN_HZ = 2000.0;

// `ceiling_gate` lets every harmonic at or below `LADDER_MEASURED_TO` past the
// ceiling, because the measured range is a floor the ceiling may not cut into. If
// the harmonic ceiling were ever set below that index the two rules would
// contradict each other and the ceiling would silently do nothing.
static_assert(STACK_CEILING_HARMONICS >= static_cast<double>(kz_audio::LADDER_MEASURED_TO),
		"the harmonic ceiling sits inside the measured ladder, so `ceiling_gate`'s "
		"floor would override it and the stack would not track rpm at all");

// The highest partial frequency allowed, as a fraction of Nyquist.
//
// A partial above Nyquist does not vanish, it folds back into the audible band at
// `sample_rate - f`, lands on a frequency that is not a harmonic of anything, and
// gets worse as rpm rises. That is the classic way a harmonic-stack synth
// acquires a metallic buzz. 0.95 keeps a margin below the fold rather than
// sitting on it, so that the fade band above has somewhere to live.
inline constexpr double NYQUIST_MARGIN = 0.95;

// Control smoothing time constant, seconds — one publish period at
// `audio_state.h`'s 120 Hz tick rate.
//
// Chosen to equal the tick rather than to be small: the smoother's job is to
// spread one tick's step over the interval until the next one, so a time constant
// of one tick reaches 63% of the new value by the time the next arrives and is
// within 0.2% after six. Smaller and the step comes back; much larger and the
// note lags the throttle audibly.
inline constexpr double CONTROL_SMOOTH_S = 1.0 / 120.0;

// Smoothing for the discrete states — shift, limiter, over-rev, clutch. A quarter
// of `kz_audio::SHIFT_TRANSIENT_S`, so that a 45 ms torque cut reaches its full
// depth in the first third of its own duration instead of being smeared into
// nothing by the control smoother above.
inline constexpr double EVENT_SMOOTH_S = kz_audio::SHIFT_TRANSIENT_S * 0.25;

// Below this rpm the whole synth fades to silence, linearly.
//
// Not a physical threshold — `engine.h` owns `stall_rpm` and this class must not
// hold a second opinion about where an engine stops. It exists for two mechanical
// reasons: a stopped engine has to be silent, and as f0 approaches zero the number
// of partials that fit under the ceiling approaches infinity, so something has to
// stop mattering before `MAX_PARTIALS` starts binding hard. 500 rpm is half of
// `Engine::stall_rpm` at the time of writing, so the fade is fully open
// everywhere the engine is actually running.
inline constexpr double IDLE_FADE_RPM = 500.0;

// How far outside `kz::POWERBAND_MIN_RPM`/`MAX_RPM` the on-pipe crossfade takes to
// open and close, rpm. 1,500 rpm puts the opening ramp at 7,500-9,000 and the
// closing one at 14,000-15,500, which brackets `Engine::hard_cut_rpm` of 14,800 —
// so an engine bounced off the limiter is still on the pipe, which is what it
// sounds like.
inline constexpr double ONPIPE_EDGE_RPM = 1500.0;

// How much louder the note gets coming on the pipe, dB, on top of the ladder
// shape change.
//
// **A tunable.** Nothing measured sets it: the corpus's only level measurement is
// the on/off *throttle* split, which is a different axis. 3 dB is a doubling of
// power and is deliberately modest, because the shape change from
// `PIPE_LADDER_DB` to `ONPIPE_LADDER_DB` is already large — the mids come up by
// 10 dB relative to the fundamental — and that shape change is what "on the pipe"
// actually sounds like.
inline constexpr double ONPIPE_LEVEL_DB = 3.0;

// How far below peak-revs level the note is allowed to fall, dB, and the rpm the
// rise is referenced to.
//
// **The synth had no rpm-to-level term at all**, which nobody noticed because the
// amplitude-sum normalization was quietly supplying one: the stack's RMS falls as
// 1/sqrt(N) and N fell from 160 partials at 3,000 rpm to 36 at 13,000, so the note
// got 5.5 dB louder across the range as a side effect of a normalization whose own
// comment describes it as a pure change of shape. Making the ceiling track rpm
// holds N nearly constant and took that accident away, which is how it was found.
// An effect worth having should not arrive from a divisor.
//
// So it is modelled, and `kz_audio::LEVEL_RISE_DB_PER_RPM_DOUBLING` is the
// measurement: +10.4 dB per doubling of rpm on the two stationary museum captures,
// agreeing to 0.1 dB, against +3.0 for the muffled chainsaw.
//
// **Referenced to peak power and applied downward only.** Anchoring at
// `kz::PEAK_POWER_RPM` and subtracting below it means the loudest thing the synth
// can produce is exactly what it produced before this term existed, so no amount
// of it can cost headroom — `tests/core/test_synth_headroom.cpp` would have to see
// the peak move, and it cannot. Anchoring at idle and adding upward would have
// been the same curve and would have blown the mix.
//
// 12 dB of range rather than the full slope, which over 3,000-13,000 rpm would ask
// for 22.6. The measured span it has to cover is 10.4 dB per doubling over a
// corpus whose own recordings span 1.06 and 1.77 doublings — 11.0 and 18.4 dB —
// and 12 sits at the low end of that on purpose: this stacks on top of the 3 dB of
// `ONPIPE_LEVEL_DB` and the constant's own comment says the two measurements
// overlap and cannot be separated. The floor bites below 5,780 rpm, which is under
// `kz::POWERBAND_MIN_RPM` and therefore below anywhere a driver spends time.
inline constexpr double RPM_LEVEL_RANGE_DB = 12.0;

// How far the note drops during a shift's torque cut, dB. **A tunable** —
// `kz_audio::SHIFT_TRANSIENT_MEASURED` is false. 9 dB is about a third of the
// perceived loudness, which is what an unloaded two-stroke does for the 50-80 ms
// the clutch is out. The duration is not this file's: the boundary holds
// `EngineAudioInput::shifting` true for as long as the cut lasts.
inline constexpr double SHIFT_CUT_DB = 9.0;

// How deep the ignition-cut chatter cuts, dB, and how square its edges are.
//
// **Tunables.** `kz_audio::LIMITER_CHATTER_MEASURED` is false and its comment says
// why: no recording in the corpus catches a limiter at all, and all four engines
// in it are carbureted period racers with nothing to catch. The *rate* is
// `kz_audio::LIMITER_CHATTER_HZ` and is equally unmeasured; these two are the
// depth and the edge shape of the same guess. `CHATTER_EDGE_K` sets the corner of
// `s / sqrt(s*s + k)`: 0.02 gives edges about a fifth of a cycle wide, which reads
// as a hard cut rather than a tremolo.
inline constexpr double LIMITER_CUT_DB = 7.0;
inline constexpr double CHATTER_EDGE_K = 0.02;

// Over-rev. **Tunables, and the whole point of them is that they are not the
// limiter.** `audio_state.h` puts it plainly: one is the engine doing its job and
// the other is money leaving, and a driver has to hear which. So an over-rev gets
// no chatter at all — the ignition is not saving anything — and instead gets a
// louder broadband layer and a slow irregular flutter, which is a mechanical
// distress signature and cannot be confused with a periodic 40 Hz cut.
inline constexpr double OVER_REV_NOISE_DB = 6.0;
inline constexpr double OVER_REV_FLUTTER_HZ = 11.0;
inline constexpr double OVER_REV_FLUTTER_DEPTH = 0.35; // linear, about +/-3 dB

// Clutch slip. **Tunables** — `kz_audio`'s "gearshift transient and the clutch"
// block says neither is measured. A slipping dry clutch is a rubbing, broadband
// noise, so it enters as a lift of the noise layer and a downward shift of that
// layer's high-pass corner, which broadens the noise out of the top octave and
// into the midrange where it can be heard over the stack.
//
// The reference slip is 200 rad/s, which is about 1,900 rpm of difference across
// the plates — roughly what a KZ launch off the line actually holds.
inline constexpr double CLUTCH_SLIP_REFERENCE_RADS = 200.0;
inline constexpr double CLUTCH_NOISE_DB = 8.0;
inline constexpr double CLUTCH_NOISE_CORNER_DROP = 0.75; // of the corner, at full slip

// Where the noise layer's one-pole high-pass sits, as a fraction of the stack
// ceiling.
//
// **A tunable, but a derived one, and the derivation is short:**
// `kz_audio::STACK_CEILING_HZ`'s comment says the cap "leaves the top octave of
// the audible band to §12's noise layer, which is where broadband two-stroke
// content belongs anyway". So the two layers are placed not to fight: the stack fills up
// to the ceiling and the noise is high-passed at half of it, a gentle 6 dB/octave
// corner that leaves the noise present but subordinate below the ceiling and
// dominant above it. Nothing measured constrains the shape — no recording in the
// corpus separates the broadband layer from the engine over the top of it.
//
// **It is a fraction of the *effective* ceiling and not of the configured one**,
// which used to be the same thing and stopped being it when the ceiling started
// tracking rpm. Leaving the corner at a fixed 4 kHz while the stack moved would
// have kept a fixed-frequency layer sitting on top of an rpm-tracking one, and
// that layer is exactly what flattened the centroid before: measured on the
// shipped build, the stack alone rose at k = 0.71 and the mix rose at k = 0.21,
// so a fixed-corner noise bed was throwing away two thirds of a brightness
// sweep the stack was already producing correctly. Half of the effective ceiling
// keeps the two layers in the relationship this comment describes at every rpm.
inline constexpr double NOISE_HIGHPASS_FRACTION = 0.5;

// Where the output soft-clips. Linear below this and asymptotic to 1.0 above, so
// `render` can never write a sample outside (-1, 1) whatever the caller does with
// `gain`.
//
// It is a safety net and not a sound: the harmonic stack is normalized so that
// the sum of its partial amplitudes equals the commanded level, which means the
// stack alone is bounded by that level analytically. Only the unmeasured state
// layers stacking up — over-rev noise on top of clutch noise on top of a raised
// `gain` — can reach the knee.
inline constexpr double SOFT_CLIP_THRESHOLD = 0.8;

// Cycle-to-cycle combustion variation, as a fraction of f0, and the corner of the
// filter that shapes it.
//
// **A tunable, and unmeasured** — nothing in `kz_audio_reference.h` constrains it,
// and it is here because of a defect a spectrum cannot see. A harmonic stack at a
// held rpm is an exactly periodic signal, and an exactly periodic signal is a
// buzzer: it is the difference between an engine and a test tone, and it is
// audible immediately on a static rev even when every harmonic level is right.
// Real cylinders do not fire identically — charge, scavenging and ignition all
// vary cycle to cycle — so f0 wanders by a fraction of a percent from one firing
// to the next, and every harmonic's phase wanders with it multiplied by its own
// index, which is what makes the top of the stack sound rough instead of glassy.
//
// 0.6% RMS at a 120 Hz corner is a starting value chosen by ear, not a
// measurement, and it is exposed through `set_combustion_jitter` so it can be
// turned while driving. It is small enough that the Nyquist margin absorbs it —
// see the static_assert below — and the spectral tests set it to zero, because a
// jittered signal has no exact bins to measure.
inline constexpr double COMBUSTION_JITTER = 0.006;
inline constexpr double COMBUSTION_JITTER_HZ = 120.0;

// The largest jitter that still cannot push a partial over Nyquist. The stack's
// top partial sits at `NYQUIST_MARGIN` of Nyquist and the jitter is clamped to
// twice its RMS, so the margin has to cover both.
inline constexpr double MAX_COMBUSTION_JITTER = (1.0 / NYQUIST_MARGIN - 1.0) * 0.5;
static_assert(COMBUSTION_JITTER * 2.0 < 1.0 / NYQUIST_MARGIN - 1.0,
		"the default combustion jitter can push the top partial past Nyquist, "
		"which is the one thing the stack is not allowed to do");

// The longest comb delay the fixed delay line can hold, seconds. 20 ms is 14
// times `kz_audio::COMB_DELAY_MEASURED_S`, which is a lot of room for the "a KZ's
// tau is longer than this, by an unknown amount" that the same constant's comment
// insists on.
inline constexpr double MAX_COMB_DELAY_S = 0.02;

} // namespace synth_tuning

// The synthesizer.
//
// One instance per engine. Everything is a fixed-size member: at 192 partials and
// a 4,096-point sine table the object is about 50 kB, which is a deliberate trade
// — the table is what keeps `render`'s inner loop to a load, a multiply-add and a
// wrap instead of a libm call per partial per sample.
class EngineSynth {
public:
	// The hard cap on the stack size.
	//
	// It is a cost bound, not an acoustic one, and since the ceiling became a
	// harmonic count it is **very nearly dead code**: the stack is 36 partials
	// wherever `STACK_CEILING_HARMONICS * f0` is the binding constraint, and only
	// the `STACK_CEILING_MIN_HZ` floor can grow it — 60 partials at 2,000 rpm, 192
	// at 625. It is kept because the floor is a tunable somebody may raise and
	// because a cap that has never fired is cheaper than the crash if it were
	// needed and absent.
	//
	// **It used to bind hard and often**, which is what it was sized for: with the
	// old fixed 8 kHz ceiling the stack was 160 partials at 3,000 rpm and 191 at
	// 2,500, and `tools/verify/synth_cost_probe.gd` measured the worst operating
	// point at 6.11% of real time for one kart and **73.3% for M7's twelve-kart
	// field**, which is an underrun. The same probe on the harmonic ceiling reports
	// 1.7-2.5% and 20-30%. That was not the reason for the change and it is the
	// largest single number that came out of it.
	static constexpr int MAX_PARTIALS = 192;

	// Sine table size. 4,096 points with linear interpolation puts the
	// interpolation error at (2*pi/N)^2 / 8 = 2.9e-7, which is -131 dB — three
	// orders of magnitude below the -50 dB alias floor the tests assert against,
	// so the table cannot be mistaken for aliasing.
	static constexpr int SINE_TABLE_SIZE = 4096;

	// Comb delay line, samples. A power of two so the wrap is a mask. 4,096 holds
	// `MAX_COMB_DELAY_S` up to 204.8 kHz, which is past any device rate that will
	// ever ask.
	static constexpr int COMB_BUFFER_SIZE = 4096;

	// PCG32 streams. ARCHITECTURE.md §8 rule 3: every user carries its own
	// explicitly seeded stream, because a shared generator makes what you hear
	// depend on how many other things drew from it. The noise layer and the
	// over-rev flutter are two users.
	static constexpr uint64_t NOISE_STREAM = 0x9E3779B97F4A7C15ULL;
	static constexpr uint64_t FLUTTER_STREAM = 0xC2B2AE3D27D4EB4FULL;
	static constexpr uint64_t JITTER_STREAM = 0xBF58476D1CE4E5B9ULL;

	EngineSynth() {
		configure(EngineAudioConfig(), 48000.0);
	}

	// The two config knobs that can be moved **while the synth is running**.
	//
	// `configure()` cannot do this job. It clears the comb line and reseeds the
	// noise RNG, which is correct when a voice is bound and is a click and a
	// discontinuity when a knob moves — and worse, it is not an audio-thread call
	// at all, because it fills three tables. These two set one scalar each and
	// touch no state, so the audio thread applies them at the top of a block.
	//
	// Both are per-sample multipliers, so a change between two blocks is a step
	// in a gain rather than in a phase. At the largest step either knob offers
	// (0.01 of depth or of noise level, `core/tuning.h`'s declared step) that is
	// far below where a level change becomes an audible click, and these are
	// knobs a person turns while listening — the alternative, ramping them, would
	// smooth a transition nobody can hear at a cost paid on the deadline.
	//
	// Deliberately not a general "apply this config": the rest of
	// `EngineAudioConfig` sizes tables and cannot move without a reset, and a
	// setter that quietly ignored half its argument is worse than not having one.
	void set_comb_depth(double depth) { comb_depth_ = clamp01(depth); }
	void set_noise_gain(double gain) { noise_gain_ = gain > 0.0 ? gain : 0.0; }
	void set_gain(double gain) { gain_ = gain > 0.0 ? gain : 0.0; }

	double comb_depth() const { return comb_depth_; }
	double noise_gain() const { return noise_gain_; }
	double gain() const { return gain_; }

	// Size everything and clear it. Not an audio-thread call: it fills the sine
	// table and the two ladder tables.
	void configure(const EngineAudioConfig &config, double sample_rate) {
		config_ = config;
		sample_rate_ = sample_rate > 1.0 ? sample_rate : 48000.0;
		inv_sample_rate_ = 1.0 / sample_rate_;
		nyquist_ = 0.5 * sample_rate_;
		nyquist_ceiling_ = nyquist_ * synth_tuning::NYQUIST_MARGIN;

		// Clamp the caller's knobs into ranges the arithmetic below assumes. A
		// negative comb depth is a notch where a peak should be and a delay longer
		// than the line would read uninitialized memory; neither should be a
		// crash, and neither should be silently honored either.
		gain_ = config_.gain > 0.0 ? config_.gain : 0.0;
		comb_depth_ = clamp01(config_.comb_depth);
		noise_gain_ = config_.noise_gain > 0.0 ? config_.noise_gain : 0.0;
		ceiling_hz_ = config_.stack_ceiling_hz > 0.0 ? config_.stack_ceiling_hz : kz_audio::STACK_CEILING_HZ;
		if (ceiling_hz_ > nyquist_ceiling_) {
			ceiling_hz_ = nyquist_ceiling_;
		}

		double delay_s = config_.comb_delay_s;
		if (delay_s < 0.0) {
			delay_s = 0.0;
		}
		if (delay_s > synth_tuning::MAX_COMB_DELAY_S) {
			delay_s = synth_tuning::MAX_COMB_DELAY_S;
		}
		comb_delay_samples_ = delay_s * sample_rate_;
		if (comb_delay_samples_ > static_cast<double>(COMB_BUFFER_SIZE - 2)) {
			comb_delay_samples_ = static_cast<double>(COMB_BUFFER_SIZE - 2);
		}

		// One-pole coefficients, expressed as the surviving fraction per sample so
		// that a whole block is one `pow` rather than `frames` multiplies.
		control_pole_ = std::exp(-inv_sample_rate_ / synth_tuning::CONTROL_SMOOTH_S);
		event_pole_ = std::exp(-inv_sample_rate_ / synth_tuning::EVENT_SMOOTH_S);

		// The over-rev flutter is a one-pole on white noise, and a one-pole that
		// slow throws away almost all of it — at 11 Hz against 48 kHz the output
		// RMS is 2.7% of the input's. Normalizing by sqrt((2-a)/a) restores unit
		// RMS so that `OVER_REV_FLUTTER_DEPTH` means what it says at any sample
		// rate rather than meaning something forty times smaller at 48 kHz than at
		// 8 kHz.
		const double flutter_a = 1.0 - std::exp(-2.0 * PI * synth_tuning::OVER_REV_FLUTTER_HZ * inv_sample_rate_);
		flutter_coeff_ = flutter_a;
		flutter_norm_ = std::sqrt((2.0 - flutter_a) / flutter_a);

		// Same normalization, same reason: the combustion jitter must be 0.6% of f0
		// at any device rate rather than 0.6% at one of them.
		const double jitter_a = 1.0 - std::exp(-2.0 * PI * synth_tuning::COMBUSTION_JITTER_HZ * inv_sample_rate_);
		jitter_coeff_ = jitter_a;
		jitter_norm_ = std::sqrt((2.0 - jitter_a) / jitter_a);

		for (int index = 0; index <= SINE_TABLE_SIZE; ++index) {
			sine_[index] = std::sin(2.0 * PI * static_cast<double>(index) / static_cast<double>(SINE_TABLE_SIZE));
		}

		// The two ladders, evaluated once per harmonic index. They do not depend on
		// anything `publish` carries — only the crossfade between them and the
		// off-throttle tilt do — so the interpolation and the `log2` happen here
		// and not in the block loop.
		for (int index = 0; index < MAX_PARTIALS; ++index) {
			const double harmonic = static_cast<double>(index + 1);
			harmonic_[index] = harmonic;
			log2_harmonic_[index] = std::log2(harmonic);
			// The **tail** fits, not the whole-range ones. See
			// `kz_audio::DECAY_DB_PER_DOUBLING_ONPIPE_TAIL`: the on-pipe ladder
			// peaks at h6 and falls for two octaves after it, so extrapolating past
			// h24 with the whole-range fit made every partial above h24 louder than
			// the one below it, which is the wrong direction and not merely a kink.
			// This only affects harmonics past the end of the table; everything
			// inside it still interpolates through the measured points.
			pipe_db_[index] = ladder_db(kz_audio::PIPE_LADDER_DB, harmonic,
					kz_audio::DECAY_DB_PER_DOUBLING_PIPE_TAIL);
			onpipe_db_[index] = ladder_db(kz_audio::ONPIPE_LADDER_DB, harmonic,
					kz_audio::DECAY_DB_PER_DOUBLING_ONPIPE_TAIL);
		}

		reset();
	}

	// One tick of solver state. Called at 120 Hz from whichever side ADR-0035 puts
	// the producer on. Stores and returns; it does no work, so it cannot make the
	// producer wait.
	void publish(const EngineAudioInput &input) {
		input_ = input;
	}

	// Silence, and a clean restart. Everything with memory is cleared: phases,
	// gains, the delay line, both generators, and the smoothed controls.
	void reset() {
		for (int index = 0; index < MAX_PARTIALS; ++index) {
			phase_[index] = 0.0;
			partial_gain_[index] = 0.0;
			gain_step_[index] = 0.0;
			amp_[index] = 0.0;
		}
		for (int index = 0; index < COMB_BUFFER_SIZE; ++index) {
			comb_[index] = 0.0;
		}
		comb_write_ = 0;
		rpm_ = 0.0;
		off_ = 0.0;
		onpipe_ = 0.0;
		shift_ = 0.0;
		limiter_ = 0.0;
		over_rev_ = 0.0;
		clutch_ = 0.0;
		noise_lowpass_ = 0.0;
		flutter_ = 0.0;
		jitter_ = 0.0;
		chatter_phase_ = 0.0;
		voiced_ = 0;
		render_count_ = 0;
		input_ = EngineAudioInput();
		// Seed zero, fixed streams: §8's determinism rule is that the same inputs
		// give the same samples, and a generator seeded from anything else — a
		// clock, an address, a global counter — is the one line that would break it.
		noise_rng_.reseed(0, NOISE_STREAM);
		flutter_rng_.reseed(0, FLUTTER_STREAM);
		jitter_rng_.reseed(0, JITTER_STREAM);
	}

	// Cycle-to-cycle combustion variation, as a fraction of f0. See
	// `synth_tuning::COMBUSTION_JITTER` for why it exists and why it is a tunable.
	// Clamped to what the Nyquist margin can absorb.
	void set_combustion_jitter(double fraction) {
		if (fraction < 0.0) {
			fraction = 0.0;
		}
		if (fraction > synth_tuning::MAX_COMBUSTION_JITTER) {
			fraction = synth_tuning::MAX_COMBUSTION_JITTER;
		}
		jitter_amount_ = fraction;
	}

	double combustion_jitter() const { return jitter_amount_; }

	// Write `frames` mono samples in [-1, 1]. The audio-thread call, and the only
	// one: no allocation, no lock, no libm call per partial, no branch that
	// depends on anything but the state already in this object.
	void render(float *out, int frames) {
		if (out == nullptr || frames <= 0) {
			return;
		}

		// --- Block-rate control update -------------------------------------------
		//
		// The scalars that shape the *spectrum* are advanced once here, to the value
		// they will have at the end of the block, and the per-partial gains are then
		// ramped linearly to match. Only the frequency is smoothed per sample,
		// because that is the one a listener hears as stepping.
		const double frame_count = static_cast<double>(frames);
		const double control_alpha = 1.0 - std::pow(control_pole_, frame_count);
		const double event_alpha = 1.0 - std::pow(event_pole_, frame_count);

		const double rpm_start = rpm_;
		const double rpm_end = rpm_ + (input_.rpm - rpm_) * control_alpha;
		// The count and the ceiling fade are computed from the *highest* f0 the
		// block will pass through, not the last one. The smoother is monotonic
		// between the two ends, so taking the maximum is what makes "never any
		// above Nyquist" true for every sample rather than only for the last one:
		// with a falling rpm the end-of-block f0 would admit a partial that was
		// above Nyquist at the start of the same block.
		const double rpm_high = rpm_start > rpm_end ? rpm_start : rpm_end;

		// `trailing` is the flag the measured ladder tilt keys off, and a shift's
		// torque cut is the same condition arriving from a different direction: the
		// engine is momentarily driving nothing. Both feed the same axis.
		const double off_target = (input_.trailing || input_.shifting) ? 1.0 : 0.0;
		off_ += (off_target - off_) * event_alpha;
		shift_ += ((input_.shifting ? 1.0 : 0.0) - shift_) * event_alpha;
		limiter_ += ((input_.on_limiter ? 1.0 : 0.0) - limiter_) * event_alpha;
		over_rev_ += ((input_.over_rev ? 1.0 : 0.0) - over_rev_) * event_alpha;

		const double slip = input_.clutch_slip < 0.0 ? -input_.clutch_slip : input_.clutch_slip;
		const double clutch_target = clamp01(slip / synth_tuning::CLUTCH_SLIP_REFERENCE_RADS);
		clutch_ += (clutch_target - clutch_) * event_alpha;

		// The on-pipe axis — #82's second acceptance criterion, "coming on the pipe
		// is audible", and the defining sound of a two-stroke. The ladder
		// crossfades from `PIPE_LADDER_DB` toward `ONPIPE_LADDER_DB`, the D-9's
		// nearly flat ladder, as the engine enters `kz::POWERBAND_MIN_RPM` ..
		// `MAX_RPM` under load.
		//
		// **This axis is the weakest-evidenced thing in the file.** The ladder set
		// and the throttle split are two different measurements and combining them
		// is an inference, not a measurement. Nothing in the corpus measured one
		// engine on and off the pipe: `PIPE_LADDER_DB` is the mean of two 1959-62
		// Tomos ladders and `ONPIPE_LADDER_DB` is a third engine entirely, the
		// 1965 D-9, and the reason to read the difference between them as "on the
		// pipe" is that the D-9 is the highest-revving engine in the corpus
		// (published 14,000 rpm peak power, a KZ's own working range) and has the
		// flattest ladder anyone measured. That is an argument from engine
		// specification, not an observation of one engine changing state. If a
		// recording of a single KZ sweeping through its powerband is ever found,
		// this is the first thing it should overturn.
		const double onpipe_target = powerband_weight(input_.rpm) * clamp01(input_.load) * (1.0 - off_target);
		onpipe_ += (onpipe_target - onpipe_) * control_alpha;

		// --- Level ---------------------------------------------------------------
		//
		// Broadband level, dB. `THROTTLE_LEVEL_DELTA_DB` is the measured on-minus-off
		// figure and it enters here as a *level*; the shape half of the same
		// measurement is the tilt below. See the class comment on which of the three
		// measured columns each half fits.
		const double level_db = -kz_audio::THROTTLE_LEVEL_DELTA_DB * off_ +
				synth_tuning::ONPIPE_LEVEL_DB * onpipe_ -
				synth_tuning::SHIFT_CUT_DB * shift_ +
				rpm_level_db(rpm_end);
		const double rpm_fade = clamp01(rpm_end / synth_tuning::IDLE_FADE_RPM);
		const double level = db_to_linear(level_db) * gain_ * rpm_fade;

		// --- The stack -----------------------------------------------------------
		const double f0_high = kz_audio::rpm_to_f0_hz(rpm_high);
		const int active = active_partials(f0_high);

		// The ceiling that the fade runs down to, and the same one `active_partials`
		// counted against a moment ago.
		const double ceiling = effective_ceiling(f0_high);

		// Off-throttle tilt, dB per doubling of harmonic number. Positive, and it
		// adds to the ladder: the fundamental takes the whole broadband drop while
		// h24 gets 4.59 doublings * 1.26 = 5.8 dB of it back, so the note gets
		// relatively brighter and thinner rather than simply quieter. That is the
		// measured finding and the reason a single load scalar cannot express it.
		const double tilt = kz_audio::OFF_THROTTLE_TILT_DB_PER_DOUBLING * off_;

		double amplitude_sum = 0.0;
		for (int index = 0; index < active; ++index) {
			const double frequency = harmonic_[index] * f0_high;
			const double db = pipe_db_[index] +
					onpipe_ * (onpipe_db_[index] - pipe_db_[index]) +
					tilt * log2_harmonic_[index];
			const double amplitude = db_to_linear(db) * ceiling_gate(frequency, index + 1, ceiling);
			amp_[index] = amplitude;
			amplitude_sum += amplitude;
		}

		// Normalize by the sum of amplitudes rather than by power. Two reasons, and
		// the first is the one that matters: the peak of a harmonic stack whose
		// phases are aligned is exactly that sum, so this makes the stack's output
		// analytically bounded by `level` and the soft clip below a genuine safety
		// net rather than the thing setting the loudness. The second is that it
		// makes the crossfade to `ONPIPE_LADDER_DB` a pure change of *shape* — the
		// level part of coming on the pipe is `ONPIPE_LEVEL_DB` and is visible as
		// a tunable instead of falling out of the arithmetic unnoticed.
		const double normalize = amplitude_sum > 0.0 ? level / amplitude_sum : 0.0;

		// Partials that have just left the stack ramp to zero over this block
		// instead of disappearing. In practice their gain is already near zero when
		// they go, because the ceiling fade took it there — this is the backstop for
		// the case where rpm moved far enough in one block to skip the fade.
		render_count_ = active > voiced_ ? active : voiced_;
		if (render_count_ > MAX_PARTIALS) {
			render_count_ = MAX_PARTIALS;
		}
		const double inverse_frames = 1.0 / frame_count;
		for (int index = 0; index < render_count_; ++index) {
			const double target = index < active ? amp_[index] * normalize : 0.0;
			gain_step_[index] = (target - partial_gain_[index]) * inverse_frames;
		}
		voiced_ = active;

		// --- The layers that are not the stack -----------------------------------
		//
		// The noise layer's corner drops as the clutch slips, which broadens it down
		// out of the top octave and into the range where a slipping clutch is
		// actually heard.
		const double corner = ceiling * synth_tuning::NOISE_HIGHPASS_FRACTION *
				(1.0 - synth_tuning::CLUTCH_NOISE_CORNER_DROP * clutch_);
		const double noise_alpha = one_pole_alpha(corner);
		const double noise_level = level * noise_gain_ *
				db_to_linear(synth_tuning::OVER_REV_NOISE_DB * over_rev_ +
						synth_tuning::CLUTCH_NOISE_DB * clutch_);

		// The ignition cut. `kz_audio::LIMITER_CHATTER_HZ` is the rate and it is
		// flagged unmeasured; this is how deep it cuts. An over-rev gets none of it.
		const double chatter_depth = (1.0 - db_to_linear(-synth_tuning::LIMITER_CUT_DB)) * limiter_;
		const double chatter_step = kz_audio::LIMITER_CHATTER_HZ * inv_sample_rate_;
		const double flutter_depth = synth_tuning::OVER_REV_FLUTTER_DEPTH * over_rev_;
		const double comb_scale = 1.0 / (1.0 + comb_depth_);

		// --- The sample loop -----------------------------------------------------
		for (int sample = 0; sample < frames; ++sample) {
			// Frequency, per sample. This is the whole of #82's first criterion:
			// the fundamental never steps at a block boundary because it is never
			// set at one.
			rpm_ = input_.rpm + (rpm_ - input_.rpm) * control_pole_;

			// Combustion jitter. Every partial derives its increment from this one
			// number, so a 0.6% wander in f0 is a 0.6% wander at h1 and a 12% wander
			// in h20's phase per cycle — which is the whole point: the top of the
			// stack goes rough while the fundamental stays in tune. Without it a
			// held rpm is an exactly periodic waveform, and an exactly periodic
			// waveform is a beep.
			double jitter = jitter_ * jitter_norm_;
			if (jitter > 1.0) {
				jitter = 1.0;
			} else if (jitter < -1.0) {
				jitter = -1.0;
			}
			const double jitter_white = jitter_rng_.next_double() * 2.0 - 1.0;
			jitter_ += (jitter_white - jitter_) * jitter_coeff_;

			const double phase_step = kz_audio::rpm_to_f0_hz(rpm_) *
					(1.0 + jitter_amount_ * jitter) * inv_sample_rate_;

			double stack = 0.0;
			for (int index = 0; index < render_count_; ++index) {
				double phase = phase_[index] + harmonic_[index] * phase_step;
				// One conditional subtract is enough: the increment is
				// h * f0 / sample_rate and every rendered partial is below Nyquist,
				// so the increment is always under 0.5.
				while (phase >= 1.0) {
					phase -= 1.0;
				}
				phase_[index] = phase;
				stack += partial_gain_[index] * table_sine(phase);
				partial_gain_[index] += gain_step_[index];
			}

			const double white = noise_rng_.next_double() * 2.0 - 1.0;
			noise_lowpass_ += (white - noise_lowpass_) * noise_alpha;
			const double noise = (white - noise_lowpass_) * noise_level;

			chatter_phase_ += chatter_step;
			while (chatter_phase_ >= 1.0) {
				chatter_phase_ -= 1.0;
			}
			const double chatter_sine = table_sine(chatter_phase_);
			const double gate = 0.5 * (1.0 + chatter_sine /
					std::sqrt(chatter_sine * chatter_sine + synth_tuning::CHATTER_EDGE_K));
			const double chatter = 1.0 - chatter_depth * gate;

			const double flutter_white = flutter_rng_.next_double() * 2.0 - 1.0;
			flutter_ += (flutter_white - flutter_) * flutter_coeff_;
			double flutter = flutter_ * flutter_norm_;
			if (flutter > 1.0) {
				flutter = 1.0;
			} else if (flutter < -1.0) {
				flutter = -1.0;
			}

			const double dry = stack * chatter * (1.0 + flutter_depth * flutter) + noise;

			// The comb. Feed-forward and not feedback: the measured ripple is
			// 1.6-2.6 dB RMS, which is a gentle filter, and a feedback comb at that
			// depth would ring instead. `kz_audio::COMB_DELAY_MEASURED_S`'s default
			// is a **measured lower bound off a 50 cc pipe and not a KZ figure** —
			// tau = 1.42 ms on the one recording of sixteen whose delay held constant
			// across quarters. A KZ's pipe is longer, so its tau is longer, by an
			// amount nobody here has measured. Raising `comb_delay_s` is the right
			// thing to do; treating the default as established is not.
			comb_[comb_write_] = dry;
			double read = static_cast<double>(comb_write_) - comb_delay_samples_;
			if (read < 0.0) {
				read += static_cast<double>(COMB_BUFFER_SIZE);
			}
			const int read_index = static_cast<int>(read) & (COMB_BUFFER_SIZE - 1);
			const int next_index = (read_index + 1) & (COMB_BUFFER_SIZE - 1);
			const double fraction = read - std::floor(read);
			const double delayed = comb_[read_index] + fraction * (comb_[next_index] - comb_[read_index]);
			comb_write_ = (comb_write_ + 1) & (COMB_BUFFER_SIZE - 1);

			const double wet = (dry + comb_depth_ * delayed) * comb_scale;
			out[sample] = static_cast<float>(soft_clip(wet));
		}
	}

	// --- Inspection. For tests and telemetry, never for the audio path. ----------
	//
	// The alias test needs to see the stack the way the synth sees it: a DFT of the
	// output cannot distinguish a partial at 23 kHz that folded to 21 kHz from a
	// legitimate partial at 21 kHz, so the assertion that nothing above Nyquist
	// contributes has to be made against these.
	int partial_count() const { return render_count_; }
	double partial_frequency(int index) const {
		return harmonic_[index] * kz_audio::rpm_to_f0_hz(rpm_);
	}
	double partial_gain(int index) const { return partial_gain_[index]; }
	double current_rpm() const { return rpm_; }
	double current_f0_hz() const { return kz_audio::rpm_to_f0_hz(rpm_); }
	double nyquist_hz() const { return nyquist_; }

	// Where the stack ends at a given fundamental. Exposed so a test can assert the
	// ceiling tracks rpm directly, rather than inferring it from a spectrum and
	// leaving open whether a flat centroid came from the ceiling or the ladder.
	double stack_ceiling_hz(double f0) const { return effective_ceiling(f0); }

	// The dB the ladder machinery says harmonic `harmonic` should sit at, relative
	// to h1, before the tilt and the crossfade. Exposed so a test can compare the
	// realized spectrum against the model as well as against the table.
	static double ladder_db(const double *ladder, double harmonic, double decay_db_per_doubling) {
		const int last = static_cast<int>(kz_audio::LADDER_POINTS) - 1;
		if (harmonic <= static_cast<double>(kz_audio::LADDER_INDEX[0])) {
			return ladder[0];
		}
		// Past the last measured index, extrapolate with the *fitted decay* and not
		// by extending the last two table entries. `DECAY_DB_PER_DOUBLING_PIPE`'s
		// comment says so explicitly: the decay is what the fit established and the
		// ladder is the sample it was fitted through.
		if (harmonic >= static_cast<double>(kz_audio::LADDER_INDEX[last])) {
			return ladder[last] + decay_db_per_doubling *
					std::log2(harmonic / static_cast<double>(kz_audio::LADDER_INDEX[last]));
		}
		// Interpolate in log-harmonic space, which is the space the decay was
		// fitted in — `LADDER_INDEX`'s comment names it. Interpolating linearly in
		// `h` instead would put h12 halfway between h8 and h16 in level, where the
		// fit puts it two thirds of the way.
		const double log_harmonic = std::log2(harmonic);
		for (int index = 0; index + 1 <= last; ++index) {
			const double low = static_cast<double>(kz_audio::LADDER_INDEX[index]);
			const double high = static_cast<double>(kz_audio::LADDER_INDEX[index + 1]);
			if (harmonic <= high) {
				const double span = std::log2(high) - std::log2(low);
				const double fraction = (log_harmonic - std::log2(low)) / span;
				return ladder[index] + fraction * (ladder[index + 1] - ladder[index]);
			}
		}
		return ladder[last];
	}

	// How much quieter than peak revs the note is at this rpm, dB. Never positive:
	// `kz::PEAK_POWER_RPM` is the anchor and everything below it is attenuated, so
	// this term cannot raise the peak output and cannot cost headroom. Public
	// because `tests/core/test_synth_headroom.cpp` reconstructs the level chain
	// term by term and a term it could not see would be a term it silently skipped.
	static double rpm_level_db(double rpm) {
		if (!(rpm > 0.0)) {
			return -synth_tuning::RPM_LEVEL_RANGE_DB;
		}
		const double db = kz_audio::LEVEL_RISE_DB_PER_RPM_DOUBLING *
				std::log2(rpm / kz::PEAK_POWER_RPM);
		if (db > 0.0) {
			return 0.0;
		}
		if (db < -synth_tuning::RPM_LEVEL_RANGE_DB) {
			return -synth_tuning::RPM_LEVEL_RANGE_DB;
		}
		return db;
	}

	// How far into the powerband the engine is, 0 to 1. Public because it is half
	// of the on-pipe axis and a test that could not see it would be asserting
	// against the whole synthesizer to check one window.
	static double powerband_weight(double rpm) {
		const double low = kz::POWERBAND_MIN_RPM;
		const double high = kz::POWERBAND_MAX_RPM;
		const double edge = synth_tuning::ONPIPE_EDGE_RPM;
		if (rpm <= low - edge || rpm >= high + edge) {
			return 0.0;
		}
		if (rpm < low) {
			return raised_cosine((rpm - (low - edge)) / edge);
		}
		if (rpm <= high) {
			return 1.0;
		}
		return raised_cosine(1.0 - (rpm - high) / edge);
	}

private:
	static constexpr double PI = 3.14159265358979323846;

	static double clamp01(double value) {
		return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
	}

	static double db_to_linear(double db) {
		return std::pow(10.0, db * 0.05);
	}

	// 0 at t <= 0, 1 at t >= 1, with zero slope at both ends. Used for every fade
	// in this file, because a linear fade has a corner and a corner in an envelope
	// is a faint tick at exactly the moment #82 says there must not be one.
	static double raised_cosine(double t) {
		if (t <= 0.0) {
			return 0.0;
		}
		if (t >= 1.0) {
			return 1.0;
		}
		return 0.5 - 0.5 * std::cos(PI * t);
	}

	// Linear below the threshold — exactly linear, so a test measuring the ladder
	// is measuring the ladder — and asymptotic to 1 above it.
	static double soft_clip(double value) {
		const double threshold = synth_tuning::SOFT_CLIP_THRESHOLD;
		const double headroom = 1.0 - threshold;
		if (value > threshold) {
			return threshold + headroom * std::tanh((value - threshold) / headroom);
		}
		if (value < -threshold) {
			return -(threshold + headroom * std::tanh((-value - threshold) / headroom));
		}
		return value;
	}

	double one_pole_alpha(double corner_hz) const {
		if (corner_hz <= 0.0) {
			return 0.0;
		}
		const double alpha = 1.0 - std::exp(-2.0 * PI * corner_hz * inv_sample_rate_);
		return alpha > 1.0 ? 1.0 : alpha;
	}

	double table_sine(double phase) const {
		const double position = phase * static_cast<double>(SINE_TABLE_SIZE);
		const int index = static_cast<int>(position);
		const double fraction = position - static_cast<double>(index);
		return sine_[index] + fraction * (sine_[index + 1] - sine_[index]);
	}

	// How many partials to sum.
	//
	// **The count comes from a frequency ceiling and not from a harmonic index**,
	// and `kz_audio::LADDER_MEASURED_TO`'s comment is the argument: a stack
	// truncated at a fixed h24 has a brick wall that slides with rpm, 800 Hz at
	// idle and 5.6 kHz at 14,000, and with a ladder this flat that is audible as
	// the note changing character across the range — #82's first criterion failed
	// for a reason that has nothing to do with stepping the frequency.
	//
	// So: at least `LADDER_MEASURED_TO` partials, more when they fit under the
	// ceiling, and **never any above Nyquist**. Where the h24 floor and Nyquist
	// disagree, Nyquist wins — the floor is an analysis limit and folding a partial
	// back into the audible band is a defect.
	// Where the stack ends at this fundamental, Hz. A harmonic count first, so the
	// spectrum's shape is fixed in harmonic space and slides up with f0; then the
	// floor, the configured absolute cap, `MAX_PARTIALS` worth of harmonics and
	// Nyquist, whichever binds.
	//
	// **One function, called by all three consumers** — the partial count, the fade
	// gate and the noise layer's corner. They were three copies of the same clamp
	// chain when the ceiling was a constant, which cost nothing because a constant
	// cannot disagree with itself. An rpm-dependent ceiling can, and a noise corner
	// that had drifted a clamp away from the stack it is supposed to sit above
	// would be inaudible as a bug and visible only as the mix being wrong.
	double effective_ceiling(double f0) const {
		double ceiling = synth_tuning::STACK_CEILING_HARMONICS * f0;
		if (ceiling < synth_tuning::STACK_CEILING_MIN_HZ) {
			ceiling = synth_tuning::STACK_CEILING_MIN_HZ;
		}
		if (ceiling > ceiling_hz_) {
			ceiling = ceiling_hz_;
		}
		const double cap_ceiling = static_cast<double>(MAX_PARTIALS) * f0;
		if (ceiling > cap_ceiling) {
			ceiling = cap_ceiling;
		}
		if (ceiling > nyquist_ceiling_) {
			ceiling = nyquist_ceiling_;
		}
		return ceiling;
	}

	int active_partials(double f0) const {
		if (!(f0 > 0.0)) {
			return 0;
		}
		const double ceiling = effective_ceiling(f0);
		int count = 0;
		for (int harmonic = 1; harmonic <= MAX_PARTIALS; ++harmonic) {
			const double frequency = static_cast<double>(harmonic) * f0;
			if (frequency >= nyquist_ceiling_) {
				break;
			}
			if (harmonic > kz_audio::LADDER_MEASURED_TO && frequency >= ceiling) {
				break;
			}
			count = harmonic;
		}
		return count;
	}

	// The fade a partial enters and leaves through. Partials at or below
	// `LADDER_MEASURED_TO` are only gated by Nyquist — that is the "at least this
	// many" floor — and everything above is gated by whichever ceiling is lower.
	double ceiling_gate(double frequency, int harmonic, double ceiling) const {
		const double limit = harmonic <= kz_audio::LADDER_MEASURED_TO
				? nyquist_ceiling_
				: (ceiling < nyquist_ceiling_ ? ceiling : nyquist_ceiling_);
		if (frequency >= limit) {
			return 0.0;
		}
		const double start = limit * (1.0 - synth_tuning::CEILING_FADE_FRACTION);
		if (frequency <= start) {
			return 1.0;
		}
		return raised_cosine(1.0 - (frequency - start) / (limit - start));
	}

	EngineAudioConfig config_{};
	EngineAudioInput input_{};

	double sample_rate_ = 48000.0;
	double inv_sample_rate_ = 1.0 / 48000.0;
	double nyquist_ = 24000.0;
	double nyquist_ceiling_ = 22800.0;
	double ceiling_hz_ = kz_audio::STACK_CEILING_HZ;
	double gain_ = 0.35;
	double comb_depth_ = 0.25;
	double noise_gain_ = 0.12;
	double comb_delay_samples_ = 0.0;
	double control_pole_ = 0.0;
	double event_pole_ = 0.0;
	double flutter_coeff_ = 0.0;
	double flutter_norm_ = 1.0;
	double jitter_coeff_ = 0.0;
	double jitter_norm_ = 1.0;
	double jitter_amount_ = synth_tuning::COMBUSTION_JITTER;

	// Smoothed controls. Every one of these is the value at the end of the last
	// block, and every one moves continuously.
	double rpm_ = 0.0;
	double off_ = 0.0;
	double onpipe_ = 0.0;
	double shift_ = 0.0;
	double limiter_ = 0.0;
	double over_rev_ = 0.0;
	double clutch_ = 0.0;

	double phase_[MAX_PARTIALS]{};
	double partial_gain_[MAX_PARTIALS]{};
	double gain_step_[MAX_PARTIALS]{};
	double amp_[MAX_PARTIALS]{};
	double harmonic_[MAX_PARTIALS]{};
	double log2_harmonic_[MAX_PARTIALS]{};
	double pipe_db_[MAX_PARTIALS]{};
	double onpipe_db_[MAX_PARTIALS]{};
	int voiced_ = 0;
	int render_count_ = 0;

	double sine_[SINE_TABLE_SIZE + 1]{};

	double comb_[COMB_BUFFER_SIZE]{};
	int comb_write_ = 0;

	double noise_lowpass_ = 0.0;
	double flutter_ = 0.0;
	double jitter_ = 0.0;
	double chatter_phase_ = 0.0;

	Pcg32 noise_rng_{ 0, NOISE_STREAM };
	Pcg32 flutter_rng_{ 0, FLUTTER_STREAM };
	Pcg32 jitter_rng_{ 0, JITTER_STREAM };
};

} // namespace kart::core

#endif // KART_CORE_ENGINE_SYNTH_H
