#ifndef KART_CORE_SCRUB_WIND_H
#define KART_CORE_SCRUB_WIND_H

#include "core/audio_state.h"
// For `synth_tuning::SOFT_CLIP_THRESHOLD` and nothing else. See `soft_clip`
// below: the alternative was retyping 0.8 here, which would give the two synth
// layers two independent ideas of where full scale is.
#include "core/engine_synth.h"
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
// **The drive is solver truth.** `EngineAudioInput::scrub` is the mean of the four
// corners' slip angles normalized against the tire's own peak, computed in
// `kart_body.cpp` from `WheelTelemetry`. `speed_ms` is road speed. `surface` is
// the `SurfaceType` a wheel is actually on, and `surface.h`'s `roughness` column
// exists for this file by name — its comment says so. None of that is invented.
//
// **The timbre is measured on the wrong objects, and every number says which.**
// This file was first written when both spectrum flags were false and everything
// below was a labeled guess. Issue #84's sourcing pass then measured a scrub band
// off three CC0 field recordings and found published wind band levels at stated
// speeds, so `kz_audio_reference.h` now carries both — with the caveats attached,
// because the scrub was measured on **passenger-car radials** and the wind under a
// **motorcycle helmet**. `docs/REFERENCES.md` §"Tire scrub and wind" has the
// corpus, the separation method and seven numbered reasons to be careful.
//
// So the defaults below are derived from a measurement of a different object
// rather than guessed, and `core/tuning.h` classifies them `Provenance::Derived`
// rather than `Unsourced` or `Measured`. Derived is not defended, which is the
// point: they still have to be turned by ear, and F2 turns them from the first
// drive.
//
// The consequence a reader should take away: **a wrong number in this file is a
// knob somebody has not turned yet, not a defect.** A wrong number in
// `kz_audio_reference.h` would be a defect. That is the whole reason the two files
// are separate — and the transfer between them, car tire to kart slick, is where
// the weak joint now lives rather than in the absence of any number at all.
//
// ## What is measured and still not built
//
// A real squeal is **intermittent**: 32 and 33 separate events in the two Chrysler
// recordings, median 85 ms, with the peak frequency moving 989-3600 Hz p10 to p90.
// This layer is continuous. `kz_audio::SCRUB_EVENT_MEDIAN_S` records the number
// and nothing reads it; issue #161 owns the event model. It is not built here
// because an event model needs a stick-slip onset criterion and inventing one on
// top of a four-corner mean would be a mechanism nobody measured — which is the
// same rule that kept this whole file from existing until somebody went and
// measured a band.
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
//
// ## Both layers end at the same ceiling the engine note does
//
// Neither class clamped its output for two milestones, and at the shipped
// `scrub_gain` of 0.45 the scrub layer wrote a peak of **1.2546** at full drive
// and 12 m/s or more. `EngineSynth` has had `soft_clip` since it was written and
// the two layers sum into one mixer, so a second, different ceiling here would
// mean the mix has two answers to "what is full scale". They now share one —
// `synth_tuning::SOFT_CLIP_THRESHOLD`, same knee, same tanh shape. See
// `soft_clip` below for why the function is repeated where the number is not.
//
// ## Both gains are levels, and that took two fixes rather than one
//
// A layer's gain is a level only if what sits between it and the output has a
// known gain of its own that is divided back out. `SCRUB_CHAIN_RMS_C` does that
// for the scrub band and its comment tells the story. **The wind layer had the
// identical defect and it was fixed a commit later**: its low-pass corner rises
// with road speed, so the filter's own white-noise gain was rising with speed
// too, and the layer realized 20.49 dB per doubling of road speed against the
// 18.0 that `kz_audio::WIND_DB_PER_SPEED_DOUBLING` cites. 2.49 dB per doubling
// was arriving from a filter rather than from the speed law, and `wind_gain`
// moved every time `wind_cutoff_hz_per_ms` did. `wind_chain_rms` is the fix.
//
// **Both constants are a filter's RMS *gain*, and neither divides out the noise
// source's own RMS. That is a deliberate choice and it leaves one thing open.**
// The source is `next_double() * 2 - 1`, uniform on [-1, 1), whose RMS is
// 1/sqrt(3) = 0.5774 — so a layer set to gain `g` renders at about `0.577 * g`
// RMS rather than at `g`. Measured, at drive 1.0 and 30 m/s on asphalt: scrub
// realizes 0.588 of its gain, wind realizes 0.577 of its gain at the reference
// speed, and the 2% between them is the `SCRUB_CHAIN_RMS_Q_EXPONENT` fit's own
// residual at the Q asphalt lands on.
//
// The two layers therefore agree with each other, which is the property #160
// needs, and both are 4.77 dB below the number written on the knob, which is a
// surprise waiting for whoever reads `scrub_gain = 0.45` and expects 0.45 RMS.
// Closing it is one factor in two places — but it moves *both* shipped levels by
// 4.77 dB and both gains have just been set by ear against how the layers
// actually sound. So it is #160's call and not a defect fix, and the offset is
// written down here rather than left to be re-derived. `SCRUB_CHAIN_RMS_C`'s own
// comment calls its table "measured RMS through the shipped chain, white noise
// in"; the numbers in it are gains against a unit-RMS source, which is the same
// convention and looser wording.

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

// Where the scrub band sits on a smooth surface, Hz.
//
// `kz_audio::SCRUB_PEAK_HZ`, named rather than retyped — the reference header owns
// the number and the argument, this file owns what is done with it. Measured on
// passenger-car radials; a kart slick is plausibly higher and "plausibly" is the
// word §5 item 10 exists to forbid, so the transfer is a knob.
//
// Listen for: too high and it is a whistle rather than a squeal; too low and it
// disappears under the engine's own noise layer.
inline constexpr double SCRUB_CENTER_HZ = kz_audio::SCRUB_PEAK_HZ;

// Q of the band-pass section, derived from the measured -10 dB width.
//
// For a two-pole band-pass the -10 dB points satisfy `1 + Q^2 x^2 = 10` where
// `x = f/f0 - f0/f`, so
//
//     Q = 3 / (2^(W/2) - 2^(-W/2))
//
// with W the width in octaves. At `kz_audio::SCRUB_WIDTH_OCT` = 0.67 that is
// 3 / 0.46862 = **6.402**. `band_q_for_width_oct` computes it rather than
// restating it, so the two cannot drift, and
// `tests/core/test_scrub_wind.cpp` measures the realized width back out of the
// filter and compares it against the recordings' figure.
//
// The measured width is an **upper** bound on one event's width — the spectra are
// aggregates over events whose peak moves 989-3600 Hz — so this Q is a lower
// bound. Listen for: too much rings like a sine on every corner, too little stops
// being distinguishable from wind.
//
// A function and not a constant, deliberately: `std::pow` is not usable in a
// constant expression here, and a namespace-scope `double` with a dynamic
// initializer would run inside `dlopen` — harmless for arithmetic, but CLAUDE.md's
// `dyld4::callInitializer` trap is close enough to it that the habit is not worth
// forming. `ScrubWindConfig` calls this from a member initializer, which runs at
// construction like any other object's.
inline double band_q_for_width_oct(double width_oct) {
	const double half = width_oct * 0.5;
	const double x = std::pow(2.0, half) - std::pow(2.0, -half);
	if (x <= 0.0) {
		return 1.0;
	}
	return 3.0 / x;
}

// The band is **asymmetric**, and a band-pass on its own cannot be.
//
// `kz_audio::SCRUB_SLOPE_BELOW_DB_OCT` and `SCRUB_SLOPE_ABOVE_DB_OCT` are +9.7 and
// -14.0, fitted over the octave below the peak and the two octaves above it. Over
// those same spans a single two-pole band-pass gives +7.9 / -8.0 — symmetric, and
// short of the upper slope by 6 dB/octave. Two cascaded band-passes give
// +15.6 / -16.0, which overshoots both. Neither shape can be asymmetric at all,
// because a band-pass is not.
//
// A one-pole low-pass **at the band center** is flat below it and adds 6 dB/octave
// above it, so it steepens the upper skirt and leaves the lower one alone. That is
// the asymmetry the recordings show, and it is the whole reason this constant
// exists. Measured through the shipped filter:
//
//     structure                          peak    width    below    above
//     1 x BP(Q=6.40)                     1000    0.667    +7.91    -8.03
//     2 x BP(Q=3.14)                     1000    0.668   +15.59   -15.97
//     BP(Q=6.40) + one-pole LP at f0     1000    0.674    +7.21   -13.28    <-
//     recordings                                  0.67    +9.70   -14.00
//
// **The lower skirt is still 2.5 dB/octave short and that is left alone
// deliberately.** Closing it needs a third stage fitted to three recordings of two
// passenger cars, analyzed from lossy previews, by one recordist in one session,
// of a tire this project does not use. That is false precision, and §5 item 10's
// spirit cuts against it as hard as it cuts against inventing the number in the
// first place.
//
// As a multiple of the band center rather than a frequency, so that moving
// `scrub_center_hz` on F2 carries the shape with it instead of turning the band
// into a different filter at one end of its range.
inline constexpr double SCRUB_TILT_LP_RATIO = 1.0;

// What the band-pass chain does to the level of white noise, so it can be divided
// back out.
//
// **Without this, `scrub_gain` is not a level control**, and that is a defect
// rather than a missing feature. Measured RMS through the shipped chain, white
// noise in, at f0 = 1 kHz:
//
//     Q      0.8    1.5    2.4    3.2   6.402   10.0    20.0
//     RMS  0.127  0.192  0.254  0.300   0.437  0.551   0.775
//
// A 6:1 swing. Turning `scrub_q` — a **timbre** knob — moved the loudness with it,
// so a driver adjusting the band's width would then have had to re-set the gain,
// and the two knobs would fight every time. Issue #160's third acceptance item is
// that turning one thing must not require re-judging another; this is that
// property inside a single layer.
//
// It also made the layer quiet in absolute terms, which is how it was found: at
// the shipped Q of 6.4 the chain passes 0.437 of white, so `scrub_gain` 0.30 was
// really 0.13 against an engine voice at 0.18.
//
// The fit is `rms = C * Q^E * sqrt(f0 / fs)`.
//
// **The exponent is 0.61 and not the 0.5 the theory gives**, and the difference is
// the tilt low-pass. The Cytomic band-pass output has a peak gain of Q rather than
// 1 and its noise bandwidth falls as 1/Q, so power goes as Q and RMS as sqrt(Q) —
// for the band-pass alone. The one-pole tilt sits at the band center, so as the
// band narrows around that corner it removes proportionally less of it, and the
// measured exponent comes out above a half. 0.5 was tried first and left a
// residual that climbed monotonically with Q, which is what a wrong exponent looks
// like as distinct from a wrong constant.
//
// The `sqrt(f0/fs)` term is the bandwidth scaling with center frequency, measured
// flat to 2.6% over 320 Hz to 2 kHz and left at the theoretical value.
//
// `tests/core/test_scrub_wind.cpp` measures the realized RMS across Q and across
// the band center and asserts both are flat, which is the property these two
// constants exist to buy.
inline constexpr double SCRUB_CHAIN_RMS_C = 0.98;
inline constexpr double SCRUB_CHAIN_RMS_Q_EXPONENT = 0.61;

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
//
// **1.0 after the first drive, down from 1.6.** 1.6 was a guess and it was the
// wrong side of that tension: the drive is a *mean over four corners*, so a kart
// with its fronts sliding and its rears gripping only reaches about half, and
// raising a half to the 1.6 gives a third. Reported as "I don't really hear much
// tire noise", which is the acceptance criterion failing. 1.0 is the identity and
// makes the layer track the drive linearly; anything below 1 is available and
// starts trading warning for a layer that is always on.
inline constexpr double SCRUB_GAMMA = 1.0;

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

// The wind low-pass's resonance. Named rather than left as a literal inside
// `render` **because the normalization below depends on it**: a second owner of
// this number would be a filter and a divisor that disagree, which is the exact
// class of defect `SCRUB_CHAIN_RMS_C` exists to close.
//
// A gentle Q, and 0.707 rather than anything higher for a reason that is not
// arithmetic: the corner is a spectral slope and not a resonance. A peak here
// would read as a whistle at one specific road speed, which is a defect that
// only shows up while driving and only at the speed that happens to hit it.
inline constexpr double WIND_FILTER_Q = 0.707;

// What the wind low-pass does to the level of white noise, so it can be divided
// back out. **This is `SCRUB_CHAIN_RMS_C`'s defect in the other layer**, and it
// went unfixed one commit longer.
//
// Without it `wind_gain` is not a level control. The corner is
// `WIND_CUTOFF_FLOOR_HZ + wind_cutoff_hz_per_ms * speed`, so it **rises with road
// speed**, and a low-pass passes more noise power the higher its corner sits.
// That gain rode on top of the speed law: measured through the shipped chain, the
// layer realized
//
//     speeds        realized dB per doubling of road speed
//     4.75 -> 9.5   20.00
//     9.5  -> 19    20.41
//     19   -> 38    20.66
//
// against `kz_audio::WIND_DB_PER_SPEED_DOUBLING` = 18.0, which is the number
// `wind_speed_exponent` is derived from. So between 2.0 and 2.7 dB per doubling
// was coming from the filter rather than from the published speed law, climbing
// toward the asymptote 18.06 + 3.01 = 21.07 as the 90 Hz floor stops mattering.
// And `wind_gain` moved whenever `wind_cutoff_hz_per_ms` moved, which is issue
// #160's third acceptance item — turning one knob must not require re-judging
// another — broken inside a single layer: winding it across its 2..120 F2 range
// moved the layer's RMS at 38 m/s by a factor of **5.208**, or 14.33 dB, with
// `wind_gain` untouched.
//
// **This is an RMS *gain*, per unit of input RMS, and that convention is
// deliberate.** `SCRUB_CHAIN_RMS_C` is the same quantity for the scrub chain, and
// the two have to agree or a scene setting both layers to the same number would
// get two different levels out — which is the exact property #160 needs and the
// whole reason either constant exists. The source's own RMS is therefore *not*
// folded in here. The file header's "Both gains are levels" section has what that
// leaves open and why it was left.
//
// **Derived, not fitted**, which is the one way this differs from the scrub
// chain. The scrub chain is a band-pass into a tilt low-pass and its exponent had
// to be measured; this is a single two-pole section with nothing after it, so the
// integral is closed-form. For `H(s) = w0^2 / (s^2 + (w0/Q) s + w0^2)`,
//
//     integral of |H|^2 over f from 0 to infinity  =  (pi / 2) * Q * f0
//
// which is the equivalent noise bandwidth. At Q = 1/sqrt(2) that is the familiar
// 1.1107 * f0 for a two-pole Butterworth. White noise carries its variance flat
// over 0 .. fs/2, so the output-to-input variance ratio is `2 * ENBW / fs` and
//
//     rms gain = sqrt(pi * Q) * sqrt(f0 / fs)
//
// which at Q = 0.707 is `1.49036 * sqrt(f0/fs)`.
//
// Measured back out of the shipped `Svf` over 2^23 samples per point, dividing
// the realized output RMS by the source's own RMS of 1/sqrt(3) and then by
// `sqrt(f0/fs)`:
//
//     f0 Hz     90     150     220     350     550     800    1078    2200    4000
//     gain  1.4843  1.4879  1.4890  1.4894  1.4897  1.4894  1.4886  1.4824  1.4697
//
// Flat to **0.4%** over 90 Hz to 1078 Hz, which is the corner's whole range at the
// default `wind_cutoff_hz_per_ms` from a standstill to the measured top speed.
// It falls away to 1.4% low at 4 kHz, reachable only by winding
// `wind_cutoff_hz_per_ms` to the top of its F2 range: that is the bilinear map
// compressing the response toward Nyquist, so the digital filter passes slightly
// less than the analog prototype it is derived from. Left at the theoretical
// value rather than fitted, for the same reason `SCRUB_CHAIN_RMS_C`'s
// `sqrt(f0/fs)` term was: 0.4% of a level is a fortieth of a dB, and a fit would
// buy nothing but a constant nobody can check against the arithmetic.
//
// A function and not a constant for `band_q_for_width_oct`'s reason: `std::sqrt`
// is not usable in a constant expression here, and a namespace-scope `double`
// with a dynamic initializer runs inside `dlopen`.
inline double wind_chain_rms(double cutoff_hz, double sample_rate) {
	if (cutoff_hz <= 0.0 || sample_rate <= 0.0) {
		return 0.0;
	}
	return std::sqrt(PI * WIND_FILTER_Q) * std::sqrt(cutoff_hz / sample_rate);
}

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
// **The exponent is now 3.0 and the story of how it got there is the useful part.**
// This constant was 2.0 and its comment said, at length, that an aeroacoustic
// dipole radiates with U^6 for an amplitude exponent of 3, that 3 is what a first
// guess reaches for, and that naming the dipole law here would be exactly the
// plausible-sounding intuition §5 item 10 exists to stop. So 2.0 was carried as an
// admitted guess instead.
//
// Then the sourcing pass found the published figures. Brown and Gordon measured
// about 20 dB per doubling of road speed under a helmet and Lower et al. measured
// 15.5 dB per doubling on the open road; `kz_audio::WIND_DB_PER_SPEED_DOUBLING`
// takes 18, and 18.06 dB per doubling *is* an amplitude exponent of 3. The refusal
// was correct and so was the number it refused — what changed is that it is cited
// now, and 2.0 was measurably too quiet at speed by 6 dB per doubling.
//
// Listen for: wind that masks the engine anywhere in the range is wrong, #84 says
// so explicitly. Wind that is inaudible at the end of the straight is also wrong.
inline constexpr double WIND_REFERENCE_SPEED_MS = 38.0;

// Amplitude exponent from a measured dB-per-doubling, because `20*log10(2^n) = n *
// 6.0206` and stating 3.0 without the conversion is how a reader ends up unable to
// check it against the paper.
inline double wind_exponent_for_db_per_doubling(double db) {
	return db / (20.0 * std::log10(2.0));
}

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

// The output ceiling, shared with the engine note rather than invented again.
//
// **The defect this closes shipped.** `ScrubSynth::render` wrote `tilt_ * level_`
// straight out with no clamp at all, and at the shipped `scrub_gain` of 0.45,
// drive 1.0 and any road speed at or above `SCRUB_FULL_SPEED_MS` the measured
// peak sample was **1.2546** on asphalt and **1.3422** on dirt — a third past
// full scale, into whatever the device does with an out-of-range float.
//
// Same threshold and same shape as `EngineSynth::soft_clip`: linear below the
// knee — exactly linear, so a test measuring a level is measuring the level —
// and `threshold + headroom * tanh((x - threshold) / headroom)` above it, which
// is asymptotic to 1.0 and C1 at the knee. The three layers sum into one mixer,
// so a different knee or a different curve here would mean the mix has two
// answers to what full scale is and the one that binds depends on which layer is
// loudest at that instant.
//
// **The number stays owned by `engine_synth.h`.** This file includes it for that
// one constant rather than retyping 0.8, because a second owner of a
// justification is how justifications rot — ARCHITECTURE.md §5 item 10 applied
// to a header. The *function* is repeated only because `EngineSynth::soft_clip`
// is a private static member: hoisting it into a shared header is the right
// shape and is a change to `engine_synth.h`, which this change does not own.
//
// It is a safety net and not a sound, with one caveat worth stating rather than
// discovering: at `scrub_gain` 0.45 the scrub layer already reaches into it at
// full drive and full speed, so it is currently doing a little work. Issue #160
// owns the level that would take it back to being idle.
inline double soft_clip(double value) {
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
	// Overall output gain, linear, per layer. **Balance, not level** — issue #160's
	// answer is that the level lives on the kart bus (`master_gain_db`), and these
	// two only say how loud each layer is against the engine note. ADR-0039.
	//
	// Both were set from measurement rather than by ear, and both moved a long way,
	// so the numbers are worth more than the usual mirror comment:
	//
	//   * **scrub 0.45 -> 0.035.** Full-slip scrub was measured 16.7 dB above the
	//     engine note in pure C++ and 16.3 dB above it through a real mix, and it
	//     peaked past full scale — 1.25 to 1.34 depending on surface, which is why
	//     the soft clip above exists now. 22 dB down puts a full-lock slide about
	//     6 dB under the engine.
	//   * **wind 0.12 -> 0.030.** This one is not a judgement changing, it is the
	//     layer moving underneath the number: normalizing the wind chain by its own
	//     filter's RMS gain made it **13 to 20 dB louder at the same gain**, most at
	//     low speed, because that is the 2.5 dB per doubling the un-normalized
	//     filter was adding. 0.12 after the fix is louder than the engine.
	//
	// `src/core/tuning.h` carries both with the same values and F2 moves them. The
	// margins are preferences and are owed an ear; the offsets are not.
	double scrub_gain = 0.035;
	double wind_gain = 0.030;

	// And the timbre, every field defaulted from the constant above it.
	//
	// **The namespace stays the single owner of the numbers and the reasoning; this
	// is a runtime copy of them.** ADR-0037's registry needs a value it can move
	// while the kart is driving, and a `constexpr` cannot be moved without a
	// rebuild — which is precisely the state issue #159 exists because of, where
	// "judged by feel" meant "whatever the first guess was" for want of a knob.
	//
	// Not every constant in `scrub_wind_tuning` is here. The ones that are are the
	// ones whose comment says *listen for* something; the rest — the rough-surface
	// band, the wind cutoff floor, the drive smoothing — are shape rather than
	// character, and adding a row for each would turn the overlay into a synth
	// editor and bury the eight that matter.
	double scrub_center_hz = scrub_wind_tuning::SCRUB_CENTER_HZ;
	double scrub_q = scrub_wind_tuning::band_q_for_width_oct(kz_audio::SCRUB_WIDTH_OCT);
	double scrub_gamma = scrub_wind_tuning::SCRUB_GAMMA;
	double scrub_full_speed_ms = scrub_wind_tuning::SCRUB_FULL_SPEED_MS;
	double wind_cutoff_hz_per_ms = scrub_wind_tuning::WIND_CUTOFF_HZ_PER_MS;
	double wind_speed_exponent =
			scrub_wind_tuning::wind_exponent_for_db_per_doubling(
					kz_audio::WIND_DB_PER_SPEED_DOUBLING);
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

	// The knobs that move while the synth runs.
	//
	// Safe on the audio thread where `configure` is not, and the distinction is the
	// same one `EngineSynth::set_comb_depth` draws: `configure` resets the filter
	// and reseeds the PRNG, which is correct when a voice is bound and is a click
	// and a discontinuity when a knob moves. These copy scalars and touch no state.
	//
	// The gain is a per-sample multiplier, so a change between blocks is a step in a
	// level; the rest are read once per block on the way into a coefficient, so a
	// change between blocks is a step in a filter that this form is well behaved
	// under. Neither is ramped, for the reason `EngineSynth` gives: at the step
	// sizes `core/tuning.h` declares, a person turning a knob while listening cannot
	// hear the transition, and smoothing it would be cost paid on the deadline for
	// nothing.
	void set_gain(double gain) { config_.scrub_gain = gain > 0.0 ? gain : 0.0; }
	double gain() const { return config_.scrub_gain; }

	void set_tuning(const ScrubWindConfig &config) {
		config_ = config;
		set_gain(config.scrub_gain);
	}

	void reset() {
		filter_.reset();
		tilt_ = 0.0;
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
		const double center = config_.scrub_center_hz +
				rough * (scrub_wind_tuning::SCRUB_ROUGH_CENTER_HZ - config_.scrub_center_hz);
		const double q = config_.scrub_q +
				rough * (scrub_wind_tuning::SCRUB_ROUGH_Q - config_.scrub_q);
		filter_.set(center, q, sample_rate_);
		// The tilt that makes the band asymmetric. See SCRUB_TILT_LP_RATIO.
		const double tilt_alpha = one_pole_alpha(
				center * scrub_wind_tuning::SCRUB_TILT_LP_RATIO, sample_rate_);

		// The target level. Drive shaped by gamma, times the speed weight the
		// dissipated-power argument requires, times the layer gain.
		double drive = input_.scrub;
		drive = drive < 0.0 ? 0.0 : (drive > 1.0 ? 1.0 : drive);
		const double shaped = std::pow(drive, config_.scrub_gamma);

		double speed_weight = input_.speed_ms /
				(config_.scrub_full_speed_ms > 1e-6 ? config_.scrub_full_speed_ms : 1e-6);
		speed_weight = speed_weight < 0.0 ? 0.0 : (speed_weight > 1.0 ? 1.0 : speed_weight);

		// Divided by what the chain does to white noise, so `scrub_gain` is a level
		// and not a level-times-whatever-Q-happens-to-be. See SCRUB_CHAIN_RMS_C.
		const double chain_rms = scrub_wind_tuning::SCRUB_CHAIN_RMS_C *
				std::pow(q, scrub_wind_tuning::SCRUB_CHAIN_RMS_Q_EXPONENT) *
				std::sqrt(center / sample_rate_);
		const double normalize = chain_rms > 1e-9 ? 1.0 / chain_rms : 1.0;

		const double target = shaped * speed_weight * config_.scrub_gain * normalize;

		// The drive one-pole, scaled to this device rate so the time constant is a
		// time and not a sample count.
		const double alpha = drive_alpha(sample_rate_);

		for (int i = 0; i < frames; ++i) {
			level_ += (target - level_) * alpha;
			const double white = rng_.next_double() * 2.0 - 1.0;
			double lp = 0.0;
			double bp = 0.0;
			filter_.process(white, lp, bp);
			// The tilt. Flat below the band center, -6 dB/octave above it, which is
			// the asymmetry `kz_audio::SCRUB_SLOPE_*` measured and which no
			// band-pass can produce on its own. Two multiply-adds.
			tilt_ += (bp - tilt_) * tilt_alpha;
			// The shared ceiling. Unclamped, this line wrote a peak of 1.2546 at
			// the shipped gain and full drive. See `soft_clip`.
			out[i] = static_cast<float>(soft_clip(tilt_ * level_));
		}
	}

	// For the tests and the HUD: the gain the layer settled at.
	//
	// **This is the multiplier applied to the filter output, not the output's own
	// level**, so it carries the chain normalization and is not bounded by 1. At
	// the shipped defaults, full drive and full speed on asphalt it settles at
	// 1.308, and the rendered RMS is `0.588 * scrub_gain` — the chain gain cancels
	// by construction and the 0.577-ish that is left is the noise source's own RMS,
	// which the file header explains. It reads as a level only if you divide it
	// back, so the honest name for it is a gain.
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

	static double one_pole_alpha(double corner_hz, double sample_rate) {
		if (corner_hz <= 0.0) {
			return 1.0;
		}
		const double alpha = 1.0 - std::exp(-2.0 * PI * corner_hz / sample_rate);
		return alpha > 1.0 ? 1.0 : alpha;
	}

	ScrubWindConfig config_{};
	EngineAudioInput input_{};
	Svf filter_{};
	Pcg32 rng_{ 0, scrub_wind_tuning::SCRUB_STREAM };
	double sample_rate_ = 48000.0;
	double level_ = 0.0;
	double tilt_ = 0.0;
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

	// Same contract as `ScrubSynth::set_tuning`: audio-thread safe, no reset.
	void set_tuning(const ScrubWindConfig &config) {
		config_ = config;
		set_gain(config.wind_gain);
	}

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
				config_.wind_cutoff_hz_per_ms * speed;
		// A gentle Q, and named because `wind_chain_rms` below is a function of it.
		filter_.set(cutoff, scrub_wind_tuning::WIND_FILTER_Q, sample_rate_);

		const double ratio = speed / scrub_wind_tuning::WIND_REFERENCE_SPEED_MS;

		// Divided by what the low-pass does to white noise, so `wind_gain` is a
		// level and `wind_speed_exponent` alone sets how that level scales with
		// speed. **The corner rises with road speed**, so without this the filter's
		// own gain rode on top of the speed law and the layer realized 20.49 dB per
		// doubling against the sourced 18.0. See `wind_chain_rms`.
		const double chain_rms = scrub_wind_tuning::wind_chain_rms(cutoff, sample_rate_);
		const double normalize = chain_rms > 1e-9 ? 1.0 / chain_rms : 1.0;

		const double target =
				std::pow(ratio, config_.wind_speed_exponent) * config_.wind_gain * normalize;

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
			// The same ceiling the scrub layer and the engine note end at. Idle at
			// the shipped `wind_gain`; this layer's peak at the reference speed is
			// about 0.30 against a 0.8 knee. It is here because the normalization
			// above raised the layer by 13.0 dB at that speed and `wind_gain` is a
			// knob with 1.0 at the top of its range. See `soft_clip`.
			out[i] = static_cast<float>(soft_clip(lp * level_));
		}
		level_ = target;
	}

	// Same caveat as `ScrubSynth::level`, and a larger one: this is the multiplier
	// on the filter output and now carries `1 / wind_chain_rms`, which at the
	// reference speed is about 4.5. It was under 1 before that fix, so anything
	// reading this expecting a 0..1 quantity is reading it wrong and was reading
	// it wrong quietly. The rendered RMS is
	// `0.577 * wind_gain * (speed/38)^exponent` — see the file header on where
	// that 0.577 comes from and why it is still there.
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
