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
// A synth that truncates at a fixed harmonic *index* has a brick wall that slides
// with rpm: at a 2,000 rpm idle f0 is 33 Hz and h24 is 800 Hz, so the note has
// nothing above 800 Hz at all, while at 14,000 rpm the same index reaches 5.6 kHz.
// With a ladder this flat the wall is audible as the note changing character
// across the range — which is precisely #82's "no audible stepping anywhere in the
// range", failed for a reason that has nothing to do with stepping the frequency.
//
// So the partial count is chosen from a **frequency ceiling** and this index is the
// floor under it: synthesize at least this many partials, more if they fit below
// the ceiling, and never any above Nyquist.
inline constexpr int LADDER_MEASURED_TO = 24;

// The frequency ceiling the stack fills to, Hz.
//
// **A tunable, and unmeasured.** Nothing in the corpus establishes where a KZ's
// spectrum ends, because every recording is lossy — Ogg Vorbis for the Commons
// files, mp3 previews for the Freesound ones — so the top of every measured ladder
// is codec-limited and is a lower bound (`REFERENCES.md` item 4 and item 10).
//
// 8 kHz is chosen for a reason that is not about the engine: at 14,000 rpm it is
// h34, comfortably past the h24 that was measured, and it leaves the top octave of
// the audible band to §12's noise layer, which is where broadband two-stroke
// content belongs anyway. It is stated here so it can be turned rather than
// discovered.
inline constexpr double STACK_CEILING_HZ = 8000.0;

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

// Tire scrub and wind. **Entirely unsourced.** §12 specifies both as filtered
// noise driven by slip and speed; no recording in the corpus isolates either,
// because every one has an engine running over the top of it. §12's claim that
// scrub "falls straight out of §6 for free" is about the *modulation* — which is
// true, `WheelTelemetry::slip_angle` and `slip_ratio` are right there — and not
// about the filter shape, which nobody has measured.
inline constexpr bool SCRUB_SPECTRUM_MEASURED = false;
inline constexpr bool WIND_SPECTRUM_MEASURED = false;

} // namespace kart::core::kz_audio

#endif // KART_CORE_KZ_AUDIO_REFERENCE_H
