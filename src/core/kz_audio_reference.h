#ifndef KART_CORE_KZ_AUDIO_REFERENCE_H
#define KART_CORE_KZ_AUDIO_REFERENCE_H

#include <cstddef>

// Measured two-stroke spectral figures, for the synthesized engine note.
//
// This file is to `ARCHITECTURE.md` §12 what `kz_reference.h` is to §6.4: the
// single owner of every externally-sourced number the audio model rests on, with
// its provenance attached and its unmeasured neighbors named rather than filled
// in. `docs/REFERENCES.md`'s "Engine audio" section is the derivation; this is the
// engineering face of it, and the two must not drift.
//
// ## Why this file exists at all
//
// ADR-0034's expensive lesson, restated by `ARCHITECTURE.md` §5 item 10 after it
// was widened: an externally-sourced constant invented in a planning session is
// indistinguishable from a measured one six months later, and this project has
// now paid for that twice. §6.4's lateral band was the first. The harmonic ladder
// below was about to be the second — §12 asks for a "harmonic stack" and every
// synthesizer's default answer is 1/n, which is **−6.02 dB per doubling of
// harmonic number**. Measurement says that is what a chainsaw does. It is the one
// engine in the corpus with an ordinary muffler and it fits at −6.7.
//
// A racing two-stroke with a tuned expansion chamber rolls off at **0.4 to 3.2 dB
// per doubling** and is still within 10 dB of its fundamental at the twenty-fourth
// harmonic. Build the stack on the default and it sounds like a chainsaw. That is
// a measurement, not a figure of speech.
//
// ## What is measured here, and what is not
//
// Measured, off named and license-cleared recordings, hash-pinned in
// `ATTRIBUTION.md`:
//
//   - the firing fundamental is rpm/60 (verified through one engine's gearbox),
//   - the per-harmonic ladders below, out to h24,
//   - how the ladder tilts between on- and off-throttle,
//   - one comb delay, on one engine, tau = 1.42 ms.
//
// **Not measured, and named as such rather than defaulted:** the comb delay for a
// KZ pipe (a lower bound only), the rev limiter's spectrum, the gearshift
// transient, the clutch, and anything at all about tire scrub or wind. Each has a
// constant below whose comment says it is a tunable, so that no caller can mistake
// one for the other by reading the code.
//
// **No recording in the corpus is known to be a KZ.** Nothing under an acceptable
// license identifies a TM, Vortex, IAME or Modena. The four two-stroke racers used
// here are identified by their spectra rather than by their captions, and the
// three whose ladders are trusted are **50 cc museum recordings**. Rev range and
// pipe design shape a two-stroke's spectrum and displacement mostly sets how much
// air moves, which is the argument for using them; it is an argument, and it is
// the weakest joint in this file.

namespace kart::core::kz_audio {

// A single-cylinder two-stroke fires once per revolution, so the firing
// fundamental is rpm/60 Hz.
//
// Verified indirectly and by an argument nothing else in a recording can imitate:
// a gearbox multiplies crank speed by a ratio the exhaust knows nothing about, so
// if f0 is really the firing rate then an upshift must step f0 by exactly that
// ratio. The Yamaha RX-100 recording is documented as accelerating through all
// four gears; exactly three clean downward f0 steps appear under power, at ratios
// **0.844, 0.905, 0.921** — monotonically increasing, which is what a real gearbox
// does and what nothing else does. Read as a two-stroke those shift points are
// 7,214 / 7,341 / 7,733 rpm against a published peak-torque speed of 7,500. Read
// as a four-stroke they would be 14,400-15,500 rpm, which a 98 cc air-cooled
// street single cannot reach.
//
// Verified on one engine. The matching negative control — that a four-stroke needs
// rpm/120 — could not be run, because the only four-stroke kart recordings contain
// several karts at once and the tracker finds a spurious low common divisor across
// them. `REFERENCES.md` item 7 holds that gap.
inline constexpr double FIRING_ORDERS_PER_REV = 1.0;

inline constexpr double rpm_to_f0_hz(double rpm) {
	return rpm * FIRING_ORDERS_PER_REV / 60.0;
}

// The harmonic indices the ladders below are sampled at.
//
// Not 1..24 contiguous: the analysis reported every harmonic to h6 and then
// thinned, because the interesting structure is the *slope* and a thinned sample
// carries it at a twelfth of the table size. A synth reading these interpolates in
// log-harmonic space between them, which is the space the slope was fitted in.
inline constexpr int LADDER_INDEX[] = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24 };
inline constexpr std::size_t LADDER_POINTS = sizeof(LADDER_INDEX) / sizeof(LADDER_INDEX[0]);

// Per-harmonic gain in dB relative to h1, median over all confidently tracked
// frames of one recording.
//
// ## Which of these a synth should use, and why it is not obvious
//
// Four two-stroke ladders were measured and they disagree by 2.8 dB per doubling.
// None of the engines is a KZ. So this is a **band, not a number**, and the choice
// below is stated rather than smuggled in as a default:
//
// `PIPE_LADDER_DB` is the mean of the **Tomos D-7 and the Colibri D-3**. Those two
// agree with each other (-2.7 and -3.2 dB per doubling), they are both racing
// expansion chambers, and they were both recorded the same way — Work With Sounds
// museum captures, stationary engine, fixed short microphone distance, which is
// the only recording geometry in the corpus whose absolute ladder shape survives
// (`REFERENCES.md` item 3: a drive-by measures the engine's spectrum times an
// unknown transfer function that moves with the vehicle).
//
// `ONPIPE_LADDER_DB` is the **Tomos D-9** alone, and it is the extreme of the
// measured range rather than an average of anything: -0.4 dB per doubling and h24
// standing 4.9 dB **above** its own fundamental. It is the flattest ladder anyone
// measured here, off the highest-revving engine in the corpus (published 14,000
// rpm peak power, which is a KZ's own working range).
//
// That the D-9 is also the file with a 1.6% frame-to-frame f0 tracking error was
// checked rather than assumed: re-fitting on only frames more than five frames
// clear of any single-step f0 change above 3% throws away 79% of its frames and
// moves the slope from -0.4 to -0.1 and h24 from +4.9 to +5.5. The flat ladder is
// not an artifact. The D-9's *rpm* readings are the part that is untrustworthy,
// and no rpm figure is taken from it.
//
// `CHAINSAW_LADDER_DB` is the negative control and exists so a test can assert the
// synth is not producing it. Stihl MS 150 C, ordinary muffler, no tuned pipe.
//
// The Yamaha RX-100 is measured (-3.6 dB per doubling, mild street chamber) and
// deliberately not carried here: it is a ridden drive-by, so item 3's objection
// applies to its absolute levels. Its *throttle split* is carried below, because
// that is a difference between two states of the same recording and the distance
// transfer function largely divides out.
//
// The Vespa PK 125 S is the only 125 cc two-stroke in the corpus and is the least
// representative thing in it — a touring silencer is the opposite of a tuned pipe,
// and its h1 stands 2.4 dB over its own noise floor, which makes every "dB re h1"
// figure derived from it meaningless. It is excluded, not deprecated.

// Tomos D-7 (1962, 50 cc, racing expansion chamber). -2.7 dB per doubling.
inline constexpr double D7_LADDER_DB[LADDER_POINTS] = {
	0.0, -1.2, -0.8, -2.7, -0.6, -1.0, -1.7, -3.4, -5.1, -6.4, -8.1, -9.3
};

// Tomos Colibri special D-3 (1959, 50 cc, racing expansion chamber). -3.2.
inline constexpr double COLIBRI_LADDER_DB[LADDER_POINTS] = {
	0.0, -2.6, -0.6, -2.9, -3.4, -0.4, 0.7, -2.3, -2.8, -9.0, -10.0, -8.8
};

// Tomos D-9 (1965, 50 cc, racing expansion chamber, 11 hp at 14,000 rpm). -0.4.
inline constexpr double D9_LADDER_DB[LADDER_POINTS] = {
	0.0, 3.5, 5.5, 5.5, 9.5, 10.3, 9.6, 9.3, 7.5, 6.2, 5.9, 4.9
};

// Stihl MS 150 C chainsaw, muffler, no tuned pipe. -6.7 — which is almost exactly
// the -6.02 of a 1/n stack. The negative control, and the thing to not sound like.
inline constexpr double CHAINSAW_LADDER_DB[LADDER_POINTS] = {
	0.0, -7.0, -5.4, -2.9, -5.1, -14.8, -15.3, -21.2, -22.8, -21.8, -23.2, -26.4
};

// The default ladder: the mean of the two agreeing museum racing pipes.
inline constexpr double PIPE_LADDER_DB[LADDER_POINTS] = {
	0.0, -1.90, -0.70, -2.80, -2.00, -0.70, -0.50, -2.85, -3.95, -7.70, -9.05, -9.05
};

// The flattest measured ladder, used as the on-pipe end of the tilt.
inline constexpr double ONPIPE_LADDER_DB[LADDER_POINTS] = {
	0.0, 3.5, 5.5, 5.5, 9.5, 10.3, 9.6, 9.3, 7.5, 6.2, 5.9, 4.9
};

// Decay, dB per doubling of harmonic number — and **there are two of these and
// they disagree**, which is stated here rather than resolved by picking one.
//
// `REFERENCES.md`'s engine-audio section publishes a fitted slope beside each
// ladder. Least squares of the tabulated ladders above against log2(h), over the
// twelve indices actually tabulated:
//
//     table       published    fit of this table    endpoint, h24/log2(24)
//     PIPE          -2.95           -1.92                  -1.97
//     D-7           -2.70           -1.95                  -2.03
//     Colibri       -3.20           -1.89                  -1.92
//     D-9           -0.40           +1.04                  +1.07
//     Chainsaw      -6.70           -6.20                  -5.76
//
// Every table's least-squares fit agrees with its own endpoint slope to about
// 0.1 dB, so the **tables are internally consistent**; it is the published slopes
// that do not describe them. The likeliest explanation is that the published fits
// were taken over the full contiguous h1-h24 data, where eighteen of twenty-four
// points sit in the falling upper half, while `LADDER_INDEX` thins to six points
// below h6 and six above and so re-weights the fit.
//
// **That explanation does not cover the D-9, whose two figures differ in sign.**
// A ladder whose h24 sits 4.9 dB *above* its h1 cannot fit a negative slope by
// any weighting. Either the published -0.4 or the tabulated +4.9 is wrong, and it
// cannot be settled from here: `REFERENCES.md` records that the analysis scripts
// live in a session scratchpad rather than in this repository, so there is
// nothing to re-run. Filed rather than guessed.
//
// **The conclusion the whole section rests on is unaffected, and is strengthened.**
// The claim is that a racing two-stroke's stack is far flatter than a muffler's,
// and that a synthesizer's default 1/n is a chainsaw. On the published figures
// that gap is 2.3 dB per doubling; on the tables it is 4.3, and the on-pipe ladder
// does not decay at all. Nothing about "build it on the default and it sounds like
// a chainsaw" depends on which set is right.
//
// The constants below are **the fits of the tables**, because they are what a
// caller extrapolating past h24 needs: using the published slope instead puts a
// 1.0 dB per doubling kink at h24 in the pipe ladder and 1.4 in the on-pipe one,
// which is a discontinuity in a curve whose whole job is to be smooth. The
// published figures are carried beside them so the disagreement stays visible.
inline constexpr double DECAY_DB_PER_DOUBLING_PIPE = -1.92; // fit of PIPE_LADDER_DB
inline constexpr double DECAY_DB_PER_DOUBLING_ONPIPE = 1.04; // fit of ONPIPE_LADDER_DB
inline constexpr double DECAY_DB_PER_DOUBLING_MUFFLER = -6.20; // fit of CHAINSAW_LADDER_DB

// The slope to **extrapolate above h24 with**, which is not the slope above.
//
// The whole-range fits are fits through a **hump**, and using one past the end of
// the table gets the sign wrong. The D-9 ladder rises to +10.3 dB at h6 and then
// falls monotonically to +4.9 at h24 — it has been descending for two octaves by
// the time the table ends. A least-squares line through all twelve points is
// dominated by the rise and comes out at **+1.04 dB per doubling**, so a caller
// extrapolating with it makes h48 *louder* than h24 when the last four measured
// points say the opposite. That is worse than the h24 kink the whole-range fits
// were chosen to avoid, because a kink is a wrong derivative and this is a wrong
// direction.
//
// The slice fitted is the **upper six tabulated indices, h8 to h24**, and the
// rule is `LADDER_INDEX`'s own: that comment says the table "thins to six points
// below h6 and six above", so the upper six is a split the data already had rather
// than one invented to get an answer. It is also the half that is past every
// ladder's pipe boost and is falling monotonically, which is the property an
// extrapolation needs.
//
//     table       whole-range fit   upper-six fit (h8..h24)
//     PIPE             -1.92                 -5.79
//     D-7              -1.95                 -4.69
//     Colibri          -1.89                 -6.90
//     D-9              +1.04                 -3.08
//     Chainsaw         -6.20                 -5.22
//
// A first cut of this used "from each table's own maximum to h24" and the test
// below caught it: the PIPE ladder's argmax is h1, because it is flat to within
// 0.5 dB out to h8 and only then falls, so that rule quietly returned the
// whole-range fit for the one ladder it mattered most for. A rule that lands on a
// different slice depending on where a table's noise put its maximum is not a
// rule.
//
// **Derived**, not measured: arithmetic over the sourced tables above, and the
// arithmetic is a least-squares fit of dB against log2(h).
// `tests/core/test_engine_synth.cpp` recomputes both fits from the tables, so
// these two constants cannot drift from the ladders they came out of without a
// test going red.
//
// Nothing below h24 changes: the synth interpolates inside the table and only
// reaches for a decay past its end.
inline constexpr double DECAY_DB_PER_DOUBLING_PIPE_TAIL = -5.79;
inline constexpr double DECAY_DB_PER_DOUBLING_ONPIPE_TAIL = -3.08;

// The index the tail fit starts at. Named so the constant, the comment and the
// test cannot disagree about which slice was fitted.
inline constexpr int LADDER_TAIL_FROM = 8;

// What `REFERENCES.md` publishes. Not used by the synth; here so the two numbers
// sit side by side and a reader cannot find one without the other.
inline constexpr double DECAY_PUBLISHED_PIPE = -2.95;
inline constexpr double DECAY_PUBLISHED_ONPIPE = -0.4;
inline constexpr double DECAY_PUBLISHED_MUFFLER = -6.7;

// Exact, and the only figure here that is arithmetic rather than measurement:
// 20*log10(1/2) is what a 1/n stack does per doubling. This is the thing not to
// sound like, and it sits within 0.2 dB of the measured chainsaw.
inline constexpr double DECAY_DB_PER_DOUBLING_ONE_OVER_N = -6.02;

// How far up the stack partials were measured to stand clear of the noise floor.
//
// **This is an analysis limit and not a synthesis spec, and the difference is the
// whole reason this comment is long.** Partials stand 10 dB or more clear of the
// inter-harmonic floor out to h24 on every usable recording, which is where the
// analysis stopped rather than where the engine did.
//
// This comment used to carry an argument against truncating at a fixed harmonic
// index, on the grounds that doing so makes "the note change character across the
// range". **The argument was wrong and the measurement below overturns it: a
// two-stroke's note changing character across the range is the entire effect, and
// a note whose character does not change is the chainsaw.** See
// `CENTROID_EXPONENT_*`. The index is still a floor — synthesize at least this
// many partials — but the ceiling above it is now a harmonic count and not a fixed
// frequency, which is what makes the brightness track rpm at all.
inline constexpr int LADDER_MEASURED_TO = 24;

// The absolute cap on the stack, Hz — a backstop, not the thing that shapes the
// note. `synth_tuning::STACK_CEILING_HARMONICS` is what sets where the stack ends
// at a given rpm; this clamps it so a runaway rpm cannot walk the stack into the
// top octave.
//
// **A tunable, and unmeasured.** Nothing in the corpus establishes where a KZ's
// spectrum ends, because every recording is lossy — Ogg Vorbis for the Commons
// files, mp3 previews for the Freesound ones — so the top of every measured ladder
// is codec-limited and is a lower bound (`REFERENCES.md` item 4 and item 10). The
// codec ceilings of the six files re-measured for `CENTROID_EXPONENT_*` are 19.0
// to 20.8 kHz, so that particular objection does not bite the centroid figures,
// only the ladder tops.
//
// 9 kHz rather than the 8 it was: the cap is meant to be a backstop and at 8 kHz
// it was the binding constraint over the top 800 rpm of the range, clipping the
// harmonic ceiling exactly where the note is supposed to be brightest. The
// harmonic ceiling reaches 8,880 Hz at `Engine::hard_cut_rpm` of 14,800, so 9 kHz
// puts the cap just clear of anywhere the engine can legally run and leaves it
// catching only a runaway. The top octave of the audible band still belongs to
// §12's noise layer, which is where broadband two-stroke content belongs anyway.
inline constexpr double STACK_CEILING_HZ = 9000.0;

// --- How fast the note brightens with rpm -----------------------------------
//
// **The measurement this whole file was missing, and the one that says what
// "scream" is.** Everything above describes the spectrum at *an* operating point.
// None of it says how the spectrum moves as the engine revs, and that motion turns
// out to be the thing that separates a racing two-stroke from every other small
// engine — including from a two-stroke with an ordinary muffler.
//
// Measured here, on the same hash-pinned corpus, by `tools/assets/fetch_kz_audio.sh`
// and the method in `REFERENCES.md` §"Engine audio": decode to mono 48 kHz, 8,192
// point Hann frames at a 2,048 hop, track f0 with the harmonic-sum estimator, and
// per frame take the power-weighted mean frequency over `CENTROID_BAND_LOW_HZ` to
// the file's own measured codec ceiling. Frames are then binned by f0 and the
// median centroid of each bin is taken, because one DFT frame of a field recording
// is one realization and a statistic off one of them is noise.
//
// The quantity carried is the **exponent k in centroid proportional to f0^k**,
// fitted between the lowest and highest f0 bin of each recording. An exponent
// rather than a ratio because every recording spans a different rev range, and a
// raw "top over bottom" figure would compare a threefold sweep against a
// twofold one. k = 1 is a spectrum whose shape is fixed in *harmonic* space and
// therefore slides bodily up with the fundamental; k = 0 is a spectrum pinned to
// absolute frequency, which is what a resonance-free muffler gives.
//
//     recording                exhaust            rpm span        centroid span    k
//     Colibri D-3 (accel.)     racing pipe        4,207-8,767     1409-3591 Hz    1.27
//     Eindhoven karts          racing pipe        6,202-13,537     862-1482 Hz    0.69
//     Tomos D-7 (accel.)       racing pipe        3,885-13,284    1556-2814 Hz    0.49
//     ---
//     Stihl MS 150 C           muffler            6,660-11,347    1256-1319 Hz    0.09
//     rental kart (4-stroke)   muffler            1,275-1,687      590-615 Hz     0.15
//
// **The two groups do not overlap and there is a factor of three between them.**
// That is the finding. A tuned expansion chamber boosts a band of *harmonic
// indices* — `ONPIPE_LADDER_DB` peaks at h6 — so as the engine revs, the boosted
// band is dragged up in absolute frequency with f0 and the note brightens. A
// muffler has no such band, its transfer function is fixed in Hz, and its centroid
// sits still.
//
// The Tomos D-9 is excluded and not for convenience: its k measures 0.13, but its
// f0 track is the one this file already refuses to take an rpm figure from — 1.6%
// of its frame transitions are tracker jumps at rational ratios, and a k is a fit
// *against* rpm. Its ladder is unaffected and is still used, exactly as before.
//
// **What does not transfer is the absolute centroid.** Eindhoven's is 862-1482 Hz
// and the Colibri's is 1409-3591 for the same class of engine, because one is
// trackside at an unknown distance through an unknown amount of air absorption and
// the other is a close museum capture. `REFERENCES.md` item 3's objection applies
// in full. The exponent survives it, because a fixed transfer function multiplies
// every frame of one recording alike and a *ratio between two rpm bands of the
// same file* divides it back out. That is the same argument the throttle split is
// carried on and it is the reason these are exponents and not spectra.
inline constexpr double CENTROID_EXPONENT_COLIBRI = 1.27;
inline constexpr double CENTROID_EXPONENT_EINDHOVEN = 0.69;
inline constexpr double CENTROID_EXPONENT_D7 = 0.49;
inline constexpr double CENTROID_EXPONENT_CHAINSAW = 0.09;
inline constexpr double CENTROID_EXPONENT_FOUR_STROKE = 0.15;

// The band a synthesized note has to land in to be a tuned-pipe two-stroke rather
// than a muffled one. The min and max of the three racing pipes above.
//
// It is a **band and not a target** for the same reason `PIPE_LADDER_DB` is: no
// recording in the corpus is a KZ, the three that set it are a 1959 50 cc racer, a
// 1962 50 cc racer and a trackside capture of somebody else's karts, and they
// disagree by a factor of 2.6. A synth inside the band is consistent with every
// tuned-pipe engine anyone here measured. A synth below
// `CENTROID_EXPONENT_MUFFLED_MAX` is measurably a chainsaw.
inline constexpr double CENTROID_EXPONENT_MIN = 0.49;
inline constexpr double CENTROID_EXPONENT_MAX = 1.27;
inline constexpr double CENTROID_EXPONENT_MUFFLED_MAX = 0.15;

// The measurement band for a centroid, Hz, and the split for the high-frequency
// fraction. Carried so that a figure quoted as "the centroid" is reproducible —
// a centroid is meaningless without the band it was taken over, and two sessions
// picking different bands would report two different numbers for one signal.
//
// The low edge is 50 Hz because below it every outdoor recording is wind and
// handling rumble rather than engine. The high edge is 16 kHz or the file's own
// codec ceiling, whichever is lower.
inline constexpr double CENTROID_BAND_LOW_HZ = 50.0;
inline constexpr double CENTROID_BAND_HIGH_HZ = 16000.0;
inline constexpr double HF_SPLIT_HZ = 4000.0;

// What fraction of total band energy sits above `HF_SPLIT_HZ` at the top of the
// rev range. Median of the highest f0 bin of each racing-pipe recording.
//
//     Eindhoven karts   6.6 %      (trackside, distant — the low end of the band)
//     Tomos D-7        23.0 %
//     Colibri D-3      42.0 %      (close museum capture, accelerating)
//
// **A wide band, and it is distance that makes it wide, not the engines.** Air
// absorption at 8 kHz is roughly 0.1 dB/m at 20 C and 50% humidity, so forty
// meters of trackside is 4 dB off the top octave before anything else happens.
// This is carried as a sanity bound — a synth outside 5-45% is wrong somewhere —
// and explicitly **not** as a target, because picking a point inside it would be
// picking a microphone distance and calling it an engine.
inline constexpr double HF_FRACTION_ONPIPE_MIN = 0.05;
inline constexpr double HF_FRACTION_ONPIPE_MAX = 0.45;

// How much louder the engine gets per doubling of rpm, dB, at a fixed microphone
// distance.
//
// **Measured, on the two recordings where a level means anything**, and the two
// agree to a tenth of a dB, which is the most consistent pair of numbers anywhere
// in this file:
//
//     recording               rpm span         band level        dB per doubling
//     Colibri D-3 (accel.)    4,207-8,767      47.2 -> 57.8 dB        +10.4
//     Tomos D-7 (accel.)      3,885-13,284     43.0 -> 61.4 dB        +10.4
//     ---
//     Stihl MS 150 C          6,660-11,347     54.9 -> 57.2 dB         +3.0
//     rental kart (4-stroke)  1,275-1,687      49.5 -> 50.0 dB         +1.0
//
// Both usable rows are stationary engines at a fixed short distance being revved
// under load, which is the only geometry in the corpus where a level change is the
// engine's. Eindhoven and Patras are trackside and their levels are dominated by
// how far away the kart was; the D-9's rpm axis is the untrustworthy one. Those
// three are excluded for reasons already stated elsewhere in this file, not for
// disagreeing.
//
// **This figure includes coming on the pipe and cannot be separated from it.** The
// Colibri's span runs straight through its own transition, so some unknown part of
// the 10.4 is `synth_tuning::ONPIPE_LEVEL_DB`'s effect measured a second time. A
// consumer applying both in full is double-counting; a consumer applying this over
// a full 3,000-14,000 rpm range would ask for 23 dB, which is more range than a
// game mix has. It is carried as the slope it is, and the clamp belongs to the
// caller.
inline constexpr double LEVEL_RISE_DB_PER_RPM_DOUBLING = 10.4;
inline constexpr double LEVEL_RISE_DB_PER_RPM_DOUBLING_MUFFLER = 3.0;

// How wide the on-pipe transition is, in rpm, and where it sits.
//
// Measured on the **Colibri D-3 accelerating frames only** — the one recording in
// the corpus that is a stationary engine at a fixed microphone distance sweeping
// under load, which is the only geometry where a spectral change over a rev sweep
// is the engine's and not the vehicle's. 10% to 90% of the total centroid rise:
//
//     rpm      1409  1879  2643  3034  3431  3557  3591      (centroid, Hz)
//     bin mid  4207  5212  6075  6900  7466  8036  8767
//
// 10% of the 1409->3674 rise is 1636 Hz and 90% is 3436 Hz, which interpolate to
// **4,700 and 7,470 rpm** — a transition **2,770 rpm wide centered on 6,085**, or
// 46% of its own center speed.
//
// **Transferring that to a KZ is not established and these two constants must not
// be read as KZ figures.** A 3.7 kW 1959 50 cc racer makes peak power somewhere
// around 7,000 rpm and a KZ makes it at 13,000; whether an expansion chamber's
// transition scales with its own tuned speed (46% of center, so ~5,300 rpm on a
// KZ) or is roughly constant in absolute rpm is exactly the kind of thing this
// corpus cannot answer. `synth_tuning::ONPIPE_EDGE_RPM` is the synth's own figure,
// it is narrower than this, and it stays a tunable.
inline constexpr double ONPIPE_TRANSITION_WIDTH_RPM = 2770.0;
inline constexpr double ONPIPE_TRANSITION_CENTER_RPM = 6085.0;
inline constexpr bool ONPIPE_TRANSITION_MEASURED_ON_KART = false;

// On-throttle minus off-throttle broadband level, dB.
//
// Measured by splitting frames on the sign of df0/dt with a +/-2%/s dead band —
// nothing but a closed throttle makes a free-revving engine lose speed — because
// no recording in the corpus carries a throttle channel.
//
// Colibri +4.6 dB and D-7 +4.3 dB, both stationary at a fixed distance. The
// Yamaha's +1.1 dB is not a third data point: the bike is riding away from the
// microphone, so its level change is contaminated by distance.
inline constexpr double THROTTLE_LEVEL_DELTA_DB = 4.45;

// How the ladder *tilts* between on and off throttle — the finding that a single
// load scalar cannot reproduce.
//
// Closing the throttle does not simply turn the note down. On the Yamaha the
// fundamental loses 10.1 dB against its local noise floor while h20-h24 lose only
// 4-5, so off-throttle the note is **relatively brighter and thinner**. The
// on-minus-off ladder difference is -5.2 dB at h2 and about 0 dB at h11.
//
// The D-7 tilts the other way at the low end — h2 and h3 are 3 dB *stronger* on
// throttle while h5-h8 are 3-4 dB weaker — so there is no single scalar that
// reproduces both, and §12's "per-harmonic gain envelopes shaped by load" is the
// right shape of model. These three columns are the only measured constraint on
// it, and they constrain a **tilt**, not a curve.
//
// Carried as a tilt in dB per doubling that adds to the base decay off-throttle:
// the fundamental drops, the top of the stack barely moves, which flattens the
// ladder. Fitted from the Yamaha's -10.1 dB at h1 against -4.5 dB at h22 over
// log2(22) = 4.46 doublings.
inline constexpr double OFF_THROTTLE_TILT_DB_PER_DOUBLING = 1.26;

// Exhaust comb delay, seconds. **A tunable with a measured lower bound and no
// upper one — do not treat this as a KZ figure.**
//
// §12 asks for a comb-filtered exhaust resonance, whose delay is a real physical
// quantity, and exactly one recording of sixteen yields a defensible one. The
// separation that makes it defensible: a pipe is bolted to the engine so its delay
// must be constant for a whole recording, while a ground reflection changes as the
// vehicle moves. Split into quarters, the D-9 gives 1.40 / 1.42 / 1.42 / 1.42 ms —
// a 1% spread, cepstral significance 7.5-8.9. Every other file spreads 17-66% and
// is measuring where the microphone was standing.
//
// The three trackside *kart* recordings are the worst offenders at 27-66%, which is
// what should be expected of a vehicle moving past a microphone, so **obtaining
// real kart recordings did not produce a kart comb delay.**
//
// What is established is tau = 1.42 ms on a 50 cc pipe tuned for 14,000 rpm. A KZ's
// pipe is physically longer at a similar peak rpm, and tuned length scales roughly
// inversely with tuned rpm and directly with in-pipe wave speed, so a KZ's tau is
// **longer than this** — by how much is unknown here. Backing a length out of it
// needs the speed of sound in exhaust gas, which was never measured: 396 mm at a
// plausible 500 C is consistent-with, not established, and the length is decoration
// anyway. tau is the honest parameter.
inline constexpr double COMB_DELAY_MEASURED_S = 0.00142;
inline constexpr double COMB_SPACING_MEASURED_HZ = 704.0;

// How deep the comb actually is, dB RMS of ripple.
//
// Measured 1.6-2.6 dB. This is the one number here that contradicts a first guess
// hard enough to be worth stating on its own: the deep metallic comb a synth
// reaches for is nothing like what the measurement shows. It is a gentle filter.
inline constexpr double COMB_RIPPLE_DB_MIN = 1.6;
inline constexpr double COMB_RIPPLE_DB_MAX = 2.6;

// --- What could not be sourced. Every one of these is a tunable. -------------
//
// Named as constants rather than left in prose so that a caller reaching for one
// reads why it is soft, and so `REFERENCES.md`'s "what stays assumed" list has a
// counterpart the compiler can see.

// The rev limiter's sound. **Unmeasured, and not represented in the corpus at
// all.** A hard limiter parks f0 on a plateau and rings; every recording here is a
// rider shifting — measured as time spent within 3% of the recording's own 99th
// percentile f0, all four engines sit at 1.5-5.1% with 1.4-4.7% spread, which is a
// sweep and not a plateau. All four are also carbureted period racers with no
// electronic limiter to catch. A KZ's limiter is an **ignition cut**, a different
// artifact entirely, and nothing here constrains it.
//
// The rate is what an ignition cut's audible character comes from and it is a
// guess. `engine.h` owns the rpm thresholds; this is only how fast the cut
// chatters.
inline constexpr double LIMITER_CHATTER_HZ = 40.0;
inline constexpr bool LIMITER_CHATTER_MEASURED = false;

// The gearshift transient and the clutch. **Unmeasured.** The shift *ratios* are
// measured on the Yamaha; what a shift sounds like is not, and neither is a
// centrifugal clutch's slip at low rpm. `engine.h` and `clutch.h` own the states;
// these are only their durations as sound.
inline constexpr double SHIFT_TRANSIENT_S = 0.045;
inline constexpr bool SHIFT_TRANSIENT_MEASURED = false;

// --- Tire scrub -----------------------------------------------------------------
//
// **Measured, and on the wrong rubber.** This entry said "entirely unsourced" for
// two milestones and that is no longer true: issue #84's sourcing pass found three
// license-clean CC0 field recordings of rubber scrubbing on asphalt, separated the
// squeal from the engine by a stated two-population power subtraction, and
// measured a band. `docs/REFERENCES.md` §"Tire scrub and wind" has the corpus, the
// method, the per-file table and seven numbered reasons to be careful with it.
//
// **Every recording is a passenger-car radial.** A KZ runs 5-inch 10-inch slicks,
// no tread pattern, high pressure, stiff carcass; squeal frequency is set by
// tread-block stick-slip and carcass resonance and both scale with the tire. So
// the *shape* is measured and the *transfer to a kart* is not, which is why
// `SCRUB_MEASURED_ON_KART_TIRE` sits below and is false. The two flags together
// say what one boolean could not, which is the same argument ADR-0037 makes for
// having four provenance classes instead of `has_source: bool`.

// Peak of the band, Hz. The median over the three CC0 originals; the two Chrysler
// files both land exactly here and the Maxima burnout is at 1587.
inline constexpr double SCRUB_PEAK_HZ = 1000.0;

// Width of the band within 10 dB of the peak, in octaves. Median over the same
// three: 0.67, 1.67 and 0.67.
//
// **This is an upper bound on the width of one squeal, not a measurement of it.**
// The spectra are aggregates over many events whose peak frequency moves — p10 to
// p90 is 989-3600 Hz on one file — and aggregating a moving narrow peak widens the
// apparent band. A single event is at least this narrow and probably narrower.
inline constexpr double SCRUB_WIDTH_OCT = 0.67;

// Least-squares slopes in dB per octave, over the octave below the peak and the
// two octaves above it. Medians over the three CC0 originals.
//
// **These are what forced a design change rather than a constant change.** A
// single two-pole band-pass has +/-6 dB/octave asymptotes and structurally cannot
// produce +9.7 and -14.0, so `scrub_wind.h` cascades two sections for +/-12 —
// which brackets the measurement instead of falling half as fast as it.
//
// The upper slope is a lower bound on how fast the real thing falls: every file
// was analyzed from a lossy mp3 preview. The Chryslers' codec ceilings are 12.8
// and 10.7 kHz, a decade of frequency above where this fit ends, so this
// particular figure is not contaminated by it.
inline constexpr double SCRUB_SLOPE_BELOW_DB_OCT = 9.7;
inline constexpr double SCRUB_SLOPE_ABOVE_DB_OCT = -14.0;

// Median duration of one squeal event, seconds, from a narrowband peak tracker at
// a 43 ms hop. 32 and 33 events in the two Chrysler files, longest 0.85 and 1.45 s,
// present in 64% and 68% of frames.
//
// **Nothing reads this yet and that is a known gap, not an oversight.** §12
// specifies scrub as filtered noise modulated by slip and the layer that ships is
// exactly that — continuous. The measurement says a real squeal is intermittent,
// and the engine-free negative control makes the point from the other side: the
// electric kart recording's tone is present in 97% of frames and is a room and a
// motor rather than stick-slip. Issue #161 owns building an event model; it is not
// built here because an event model needs a stick-slip onset criterion, and
// inventing one on top of a four-corner mean would be a mechanism nobody measured.
inline constexpr double SCRUB_EVENT_MEDIAN_S = 0.085;

inline constexpr bool SCRUB_SPECTRUM_MEASURED = true;
inline constexpr bool SCRUB_MEASURED_ON_KART_TIRE = false;

// --- Wind -----------------------------------------------------------------------
//
// **Not measured here; published, and on a motorcycle.** No recording of
// on-vehicle wind at a *stated speed* exists under an acceptable license — the
// search is listed in REFERENCES.md item 14 and the one on-vehicle hit turned out
// to be a parked car in a storm, which has no airspeed and therefore cannot be
// scaled.
//
// What was found instead is peer-reviewed and open access: Brown and Gordon, "Motorcycle
// Helmet Noise and Active Noise Reduction", The Open Acoustics Journal 4, 14-24
// (2011), Figure 3 — a Neumann KU-100 dummy head in a half helmet on a Kawasaki
// EX500, one-third octaves at seven GPS-checked road speeds. Corroborated on the
// speed law by Lower et al., Proc. Institute of Acoustics 16(2), 319-325 (1994).
//
// Hence two flags rather than one: `WIND_SPECTRUM_MEASURED` stays **false**
// because this repository measured nothing, and `WIND_SPECTRUM_SOURCED` is true
// because an external authority published it. That is precisely ADR-0037's
// Sourced-versus-Measured distinction, and this is the first place in the codebase
// where the two disagree about the same quantity.
//
// **The level does not transfer and must not be used.** Lower et al. found the
// dominant source was the turbulent shear layer off the *windscreen's* wake
// striking the rider, and that a helmet's noise ranking reverses with screen
// height. A kart has no windscreen. The shape and the speed law are the
// transferable parts, and both have a physical argument under them.

// Slope above the plateau, dB per octave. The paper's own text: "levels tended to
// decrease at a rate of about 10 dB per octave in the range of 250 Hz to 8 kHz",
// and the transcribed figure gives -9 over 250 Hz-1 kHz and -11.5 over 1-4 kHz.
inline constexpr double WIND_SLOPE_DB_PER_OCT = -10.0;

// Where the low plateau sits, Hz. At 120 km/h the paper names 108 dB at the 100,
// 125 and 200 Hz thirds and states every band from 50 to 400 Hz exceeds 100 dB.
inline constexpr double WIND_PLATEAU_HZ = 150.0;

// How much louder wind gets per doubling of road speed, dB.
//
// Brown and Gordon: "uniformly from 200 Hz to 10,000 Hz, the noise level increased
// about 20 dB as velocity was incremented from 60 km/h to 120 km/h", and the
// transcribed rows give 16-20 dB across that range. Lower et al. §4.7 measured
// **15.5 dB per doubling** on the open road above ~25 m/s. 15.5 to 20 brackets the
// v^6 aeroacoustic dipole law, which is 18.06 dB per doubling, from both sides.
//
// 18 is taken as the figure. Note what that means for `scrub_wind.h`: an amplitude
// exponent of 3.0, which is exactly the dipole number that file originally
// **refused** to use on the grounds that it was a plausible-sounding intuition
// rather than a measurement. The refusal was right and the number was right; the
// difference is that it is now cited.
inline constexpr double WIND_DB_PER_SPEED_DOUBLING = 18.0;

inline constexpr bool WIND_SPECTRUM_MEASURED = false;
inline constexpr bool WIND_SPECTRUM_SOURCED = true;

} // namespace kart::core::kz_audio

#endif // KART_CORE_KZ_AUDIO_REFERENCE_H
