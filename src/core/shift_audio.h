#ifndef KART_CORE_SHIFT_AUDIO_H
#define KART_CORE_SHIFT_AUDIO_H

#include "core/audio_state.h"
#include "core/pcg32.h"
// For `Svf` and `soft_clip`, and for nothing else. Both are `kart::core` members
// that `scrub_wind.h` happens to own; hoisting them into a shared DSP header is
// the right shape and is a change to a file this change does not own, so they are
// reused where they are. `scrub_wind.h` pulls `engine_synth.h` in for
// `synth_tuning::SOFT_CLIP_THRESHOLD`, which is how all three layers end up
// sharing one answer to "what is full scale".
#include "core/scrub_wind.h"

#include <cmath>
#include <cstdint>

// The gearshift and the clutch. Issue #83, ARCHITECTURE.md §12.
//
// Nothing here may include godot-cpp. ADR-0017. `src/audio/noise_voice.{h,cpp}`
// is the join to Godot's mixer and renders this as one more layer, exactly as it
// renders `ScrubSynth` and `WindSynth`.
//
// ================================================================================
// WHAT IS SOURCED HERE AND WHAT IS NOT. READ THIS BEFORE THE NUMBERS.
// ================================================================================
//
// **The behavior is solver truth. The timbre is derived from structural acoustics,
// with two estimated inputs. An attempt to measure it off the corpus failed, twice,
// and both failures are recorded below.** Those are three different sentences and
// this file's whole honesty rests on keeping them apart.
//
// ## Sourced: when this layer makes a sound, and for how long
//
//   * `Gearbox::shift_time = 0.065 s` -- `gearbox.h` owns it, and every timing
//     constant below is written as a fraction of it rather than as a duration, so
//     a change there moves this file and cannot desynchronize from it.
//   * `EngineAudioInput::shifting`, `gear`, `clutch_slip`, `throttle`, `rpm` --
//     all filled at the boundary in `KartBody::publish_engine_audio()` from
//     `VehicleTelemetry`. Not invented, not defaulted.
//   * The **direction** of a shift, which costs nothing to get right and would be
//     invented if it were guessed: `drivetrain.h` publishes `gearbox.gear()`,
//     which is the OLD gear for the whole of the shift and flips to the new one at
//     the end. So latching the gear at the rising edge and comparing it at the
//     falling edge yields up-or-down with no new field and no new state in the
//     solver. See `ShiftSynth::render`.
//
// ## Derived: what a shift and a clutch actually sound like
//
// `kz_audio::SHIFT_TRANSIENT_MEASURED` is **false** and it stays false -- no
// recording constrains this. But "no recording" is not the same as "no source of
// truth", and treating it that way was the first version's mistake. A struck steel
// part rings at the modes of the parts involved, and those follow from geometry and
// material. The two bands are therefore **computed** from this project's own
// committed crankcase and clutch dimensions, with the arithmetic, the boundary
// conditions and the sensitivity to every estimated input written out in the
// physics section immediately below. They are not typed constants: `ShiftAudioConfig`
// calls the derivation, so a band cannot drift from the reasoning that justifies it,
// and `test_shift_audio.cpp` recomputes both from the formula independently.
//
// **Two inputs remain estimated** -- the crankcase wall thickness and the clutch
// steel thickness -- so the honest mark is `derived; two inputs estimated`, and the
// sensitivity tables say exactly what that costs. Arithmetic on a guess produces a
// guess with more decimal places, and this file does not pretend otherwise; what it
// buys is that a wrong band now has a wrong *input* somebody can point at.
//
// ### The two measurements that were attempted, and why both were withdrawn
//
// This is recorded rather than omitted, because "we tried and it did not work" is
// a result and the next person will otherwise try the same thing.
//
// The corpus contains one recording documented to hold clean gearshifts -- the
// Yamaha RX-100, which `kz_audio_reference.h` already used to prove that f0 is the
// firing rate, via exactly three downward f0 steps at ratios 0.844 / 0.905 /
// 0.921. A detector was built to find those steps and measure the level dip in the
// pipe band around each one. Three cuts:
//
//   1. A raw frame-to-frame f0 ratio found **44** steps on a recording with three.
//      Those were pitch-tracker harmonic errors.
//   2. Demanding f0 be *stable* either side found **zero** -- and the zero was not
//      evidence, because the recording is documented as *accelerating through all
//      four gears* and an accelerating engine's f0 is rising by construction.
//   3. Fitting the rising log-f0 trend and looking for a downward departure from
//      it found 9 events on the two gearboxed recordings. **And 48 on the three
//      single-speed engines in the corpus**, which have no gearbox and cannot
//      upshift at all. The negative control killed it.
//
// **There is a reason trackside recordings can never settle this, and it is
// arithmetic.** A vehicle passing a fixed microphone Doppler-shifts by
// `(c - v)/(c + v)`. At c = 343 m/s that ratio is 0.864 at v = 25.0 m/s -- ordinary
// kart pass speed, and dead in the middle of the 0.84-0.92 band a consecutive-gear
// step occupies. A pass and an upshift are **the same downward f0 ratio**, and both
// trackside kart recordings in the corpus (Eindhoven, Patras) are passes. No
// detector separates them from a single microphone. A shift spectrum needs an
// onboard or stationary recording of a **sequential dog box**, and the corpus has
// none under an acceptable license.
//
// The second attempt, and its own negative control, is described in "What was tried
// and did not work" at the end of the physics section: a spectral-flux onset
// detector, which Doppler does *not* defeat, and which was killed instead by the
// Stihl chainsaw firing more impulsive events per second than any gearbox in the
// corpus -- a cutting chain being a train of impacts.
//
// So neither band rests on a recording, and neither is a bare guess. They rest on
// structural acoustics over this project's own committed dimensions.
// `tools/verify/audio.sh` gates the *behavior*, which is solver truth, and
// `test_shift_audio.cpp` gates the *derivation*, which is arithmetic.

namespace kart::core {

namespace shift_tuning {

// --- the physics the two bands are derived from ---------------------------------
//
// **This section replaced two bare estimates.** The bands used to be 2200 Hz and
// 520 Hz with a paragraph of judgment each. They were reasonable and they were
// unfalsifiable. A struck steel part does not ring wherever a programmer thinks it
// should -- it rings at the modes of the parts involved, and those follow from
// geometry and material. So the numbers below are `derived`, the arithmetic is
// here, and `test_shift_audio.cpp` recomputes it rather than trusting it.
//
// **The inputs are not all sourced, and the label says so.** Arithmetic on a guess
// produces a guess with more decimal places; what this buys is not certainty but a
// *structure* -- a wrong answer now has a wrong input you can point at, and the
// sensitivity tables below say which input matters. Every input carries its own
// mark. The honest summary is **derived; two inputs estimated**.
//
// ## The group every thin-plate frequency is built on
//
//     c_p = sqrt(E / (12 (1 - nu^2) rho))
//
//     steel      E 200 GPa, rho 7850, nu 0.30   ->  1527.5 m/s
//     aluminium  E  70 GPa, rho 2700, nu 0.33   ->  1557.1 m/s
//
// Those are within 2% of each other, because E/rho is close for the two metals.
// It is worth knowing: it means the band is set by **geometry**, and a case cast
// in magnesium instead would not move it much.
//
// A thin plate of span `a` and thickness `t` then has modes at
//
//     f = (lambda / 2pi) * t * c_p / a^2
//
// with `lambda` a shape factor depending only on the boundary conditions and the
// aspect ratio. It is linear in thickness and inverse-square in span, which is
// what makes the sensitivity tables below short.
//
// ## 1. The dog ring is the source and is NOT the band
//
// The obvious move is to compute the dog ring's own resonance and stop. Doing it
// is what shows why it is wrong. A stubby steel ring's in-plane flexural modes are
//
//     f_n = [n(n^2-1) / (2pi sqrt(n^2+1))] * (h / R^2) * sqrt(E / 12 rho)
//
//     R 20 mm, radial depth 6 mm   n=2   9,334 Hz    n=3  26,401 Hz
//     R 25 mm, radial depth 6 mm   n=2   5,974 Hz    n=3  16,896 Hz
//     R 20 mm, radial depth 8 mm   n=2  12,445 Hz    n=3  35,201 Hz
//
// Nine to twenty-five kilohertz. A synth built on that would be a tick, and a
// listener would immediately say it sounds like a relay rather than a gearbox.
//
// The reason is that a dog ring is a small, stiff, low-area part **buried inside a
// case**. It has almost no radiating area and it is not coupled to air; the
// crankcase it is bolted inside has both. So the ring is the **excitation** -- a
// short, effectively flat impulse well above the audible band of interest -- and
// the case is the **radiator**, which is what sets the spectrum you hear. This is
// ordinary impact acoustics and it is the single most useful thing in this header.
//
// ## 2. The crankcase side wall sets the clack band
//
// Committed geometry, from this project's own spec rather than invented here:
// `powertrain.py` carries `CRANKCASE_HEIGHT = 0.140`, `CRANKCASE_FRONT_Y = -0.145`
// and `CRANKCASE_REAR_Y = -0.345`, so the side wall is **140 x 200 mm**.
//
//     unribbed 140x200 bay, a/b 1.43, clamped, lambda 57.6, t 5 mm  ->  1,784 Hz
//     ribbed into a 140x140 bay,      clamped, lambda 35.99, t 5 mm ->  2,275 Hz
//
// A cast crankcase is heavily ribbed, which divides the wall into smaller bays and
// pushes it to the upper figure. **2,275 Hz**, and `CLACK_CENTER_HZ` is now
// computed from those inputs rather than typed.
//
// Sensitivity to the one estimated input:
//
//     t 4 mm -> 1,820 Hz    t 5 mm -> 2,275 Hz    t 6 mm -> 2,730 Hz
//
// So the wall thickness is the whole uncertainty, and it is a factor of 1.5 across
// a plausible range. The old estimate of 2200 Hz sits 3% from the derived value,
// which means the judgment behind it was good -- and that is a fine outcome, not a
// wasted exercise. What changed is that it can now be argued with.
//
// ## 3. The clutch band is the steel plates' own mode
//
// Committed geometry: `docs/kart_spec/30-powertrain.md` gives the clutch bell at
// **r 60 mm** -- itself marked `estimated` there, read off the R2 reference
// photographs, and that mark is inherited here rather than laundered away. The
// steel plates key to the hub and run inside that bore, so their outer radius is
// 50-55 mm. They are close to free-free: a clutch plate hangs on its splines.
//
// Free circular plate, two nodal diameters, lambda = 5.253:
//
//     outer r 50 mm, t 1.5 mm  ->  766 Hz
//     outer r 52 mm, t 1.5 mm  ->  708 Hz
//     outer r 55 mm, t 1.5 mm  ->  633 Hz
//
// **708 Hz** at the mid radius, against the old estimate of 520 Hz -- a 36% move,
// and the one place this exercise changed something a listener will hear. The
// friction discs are deliberately not used: their lining damps them, so the steels
// are what rings.
//
// Sensitivity, at r 52 mm: t 1.2 -> 567 Hz, t 1.5 -> 708, t 1.8 -> 850, t 2.0 -> 945.
//
// ## 4. What the plate COUNT buys, which is the Q and not the centre
//
// `clutch.h` records the real pack: *"five friction discs and five steels on an
// IAME Screamer"*. That is ten rubbing interfaces -- ten stick-slip sources at
// slightly different radii and clamp loads, exciting a set of nominally identical
// plates whose manufacturing tolerances spread their modes apart. That is a
// **broadening** argument and not a centre frequency, and it is the reason `Q`
// stays under 1 and the layer reads as a rasp rather than as a tone. Using the
// plate count to compute a centre frequency would have been the laundering mistake
// this header exists to avoid.
//
// ## What was tried and did not work
//
// An independent corpus cross-check was attempted and is **inconclusive**, and the
// negative control is why. Doppler destroys a frequency-RATIO inference but not the
// arrival of impulsive energy, so a spectral-flux onset detector should in
// principle find dog engagements. Run on the corpus it found 0.99 events/s on the
// Yamaha and 0.08/s at Eindhoven -- against **1.33/s on the Stihl chainsaw**, which
// has no gearbox at all. A chainsaw's cutting chain is a train of impacts, so it
// produces exactly the signal class being detected. The detector is therefore not
// specific to gearshifts and its band cannot confirm or refute anything here.
// Recorded rather than dropped, because the next person will try the same thing.
// `scratch/transient.py` is the run.

// Material and shape factors. `sourced` -- textbook values for steel and cast
// aluminium, and Leissa's tabulated plate eigenvalues.
inline constexpr double STEEL_E_PA = 200.0e9;
inline constexpr double STEEL_RHO = 7850.0;
inline constexpr double STEEL_NU = 0.30;
inline constexpr double ALUMINIUM_E_PA = 70.0e9;
inline constexpr double ALUMINIUM_RHO = 2700.0;
inline constexpr double ALUMINIUM_NU = 0.33;

// Clamped rectangular plate, fundamental, square bay. Leissa, Vibration of Plates.
inline constexpr double CLAMPED_SQUARE_LAMBDA = 35.99;
// Free circular plate, two nodal diameters, no nodal circles.
inline constexpr double FREE_CIRCULAR_LAMBDA_2_0 = 5.253;

// `derived` -- `powertrain.py`'s `CRANKCASE_HEIGHT`, which is this project's own
// committed dimension. The rib spacing that makes the bay square is `estimated`;
// see the header.
inline constexpr double CASE_BAY_SPAN_M = 0.140;
// `estimated` -- no drawing of a KZ crankcase section is available. 5 mm is an
// ordinary sand-cast wall for a part of this size, and the header's sensitivity
// table is the honest statement of what that costs.
inline constexpr double CASE_WALL_THICKNESS_M = 0.005;

// `derived` from the clutch bell's r 60 (kart_spec 30-powertrain), which is itself
// `estimated` from photographs -- so this inherits that mark.
inline constexpr double CLUTCH_PLATE_RADIUS_M = 0.052;
// `estimated` -- an ordinary clutch steel. See the header's sensitivity table.
inline constexpr double CLUTCH_PLATE_THICKNESS_M = 0.0015;

// The plate-speed group, m/s. A function rather than a constant because `sqrt` is
// not usable in a constant expression here, and a namespace-scope `double` with a
// dynamic initializer runs inside `dlopen` -- CLAUDE.md's `dyld4::callInitializer`
// trap is close enough that the habit is not worth forming. Same reasoning
// `scrub_wind.h::band_q_for_width_oct` gives for itself.
inline double plate_speed(double e_pa, double rho, double nu) {
	return std::sqrt(e_pa / (12.0 * (1.0 - nu * nu) * rho));
}

// A thin plate's mode, Hz: `f = (lambda / 2pi) * t * c_p / a^2`.
inline double plate_mode_hz(double lambda, double thickness_m, double span_m,
		double e_pa, double rho, double nu) {
	const double a = span_m > 1e-9 ? span_m : 1e-9;
	return lambda / (2.0 * 3.14159265358979323846) * thickness_m *
			plate_speed(e_pa, rho, nu) / (a * a);
}

// A thin ring's in-plane flexural mode n, Hz. Not used by the synth -- it is here
// because computing it is what proves the dog ring is the source and not the band,
// and a claim that only lives in a comment is a claim nobody can check.
// `test_shift_audio.cpp` calls it.
inline double ring_mode_hz(int n, double radial_depth_m, double mean_radius_m,
		double e_pa, double rho) {
	const double nn = static_cast<double>(n);
	const double r = mean_radius_m > 1e-9 ? mean_radius_m : 1e-9;
	const double k = nn * (nn * nn - 1.0) /
			(2.0 * 3.14159265358979323846 * std::sqrt(nn * nn + 1.0));
	return k * (radial_depth_m / (r * r)) * std::sqrt(e_pa / (12.0 * rho));
}

// --- the dog engagement clack ---------------------------------------------------
//
// A sequential dog box does not synchronize. Three or six dogs slam into three or
// six slots under whatever torque is present, and the sound is an impact in a thin
// aluminum case. There are two of them per shift and they are not the same:
// pulling the dogs out is quiet, driving them in is not.

// Where the clack's energy sits, Hz.
//
// **`derived`; one input estimated.** The fundamental of a ribbed bay of the
// crankcase side wall -- the part that actually radiates the impact, for the reason
// section 2 of this file's physics header gives at length. Computed from the
// geometry rather than typed, so it cannot drift from the derivation that justifies
// it, and `test_shift_audio.cpp` recomputes it from the formula independently.
//
//     2,275 Hz at the shipped inputs (140 mm bay, 5 mm wall, cast aluminium)
//
// The wall thickness is the estimated input and it is the whole uncertainty: 4 mm
// gives 1,820 Hz and 6 mm gives 2,730 Hz. The rest is this project's own committed
// `CRANKCASE_HEIGHT`.
//
// Listen for: too high and it is a tick like a relay; too low and it disappears
// into the note it is supposed to interrupt.
inline double clack_center_hz() {
	return plate_mode_hz(CLAMPED_SQUARE_LAMBDA, CASE_WALL_THICKNESS_M, CASE_BAY_SPAN_M,
			ALUMINIUM_E_PA, ALUMINIUM_RHO, ALUMINIUM_NU);
}

// How broad the clack band is.
//
// **Still `estimated`, and deliberately so -- this one is NOT a mode.** A single
// mode of a single bay would have a Q of 20 or more and would ring like a bell. A
// crankcase has hundreds of modes above 2 kHz and an impulse excites all of them,
// so what a listener hears is the **modal envelope** rather than any one resonance,
// and the envelope is broad. Q 1.2 is a statement about modal density, which is not
// something the plate formula gives.
//
// Above about Q 4 an impulse through a band-pass rings and becomes a tone, which is
// the single most common way a synthesized impact sounds like a toy.
inline constexpr double CLACK_Q = 1.2;

// How fast the clack decays, as a fraction of `Gearbox::shift_time`.
//
// **Derived from a sourced duration, with the arithmetic shown**, which is the one
// timing constant here that is not a guess. The engagement impact has to be over
// before the torque comes back or it is not an impact, it is a texture. With a
// time constant of `CLACK_DECAY_FRACTION * shift_time` the burst is down
// `20*log10(exp(-1/f))` dB after one shift_time: at 0.14 that is
// `20*log10(exp(-7.14))` = **-62 dB**, i.e. gone. Anything above about 0.3 leaves
// audible energy at the moment drive returns and smears the two events together.
//
// At the shipped `shift_time` of 0.065 s this is a 9.1 ms time constant, which is
// a short, dry knock.
inline constexpr double CLACK_DECAY_FRACTION = 0.14;

// The two impacts' relative levels, linear, against the layer gain.
//
// **Estimated**, and the ratio is the part with an argument behind it rather than
// the absolute values. Disengagement happens with the driver's foot still in it
// and the dogs unloading; engagement happens with the crank and the road at two
// different speeds and the dogs closing the gap. The second is the loud one and
// the first is barely there. 0.35 is roughly 9 dB down, which is "present but not
// a second event" -- at parity a shift reads as a double knock, which no dog box
// does.
inline constexpr double CLACK_ENGAGE_GAIN = 1.00;
inline constexpr double CLACK_DISENGAGE_GAIN = 0.35;

// How much harder an upshift hits than a downshift, linear.
//
// **Estimated.** A clutchless upshift is made at full throttle against a rising
// engine and the dogs close under drive. A downshift is made off the throttle with
// the engine being dragged up to speed, so the impact is against engine braking
// rather than against power. 0.7 is a modest difference on purpose: it has to be
// audible as a difference without turning the downshift into a different event.
inline constexpr double CLACK_DOWNSHIFT_SCALE = 0.70;

// How much of the clack's level comes from how hard the engine was working.
//
// **Estimated**, and it is the one constant here whose *shape* is not arbitrary: a
// dog impact is driven by the torque across the dogs, and `EngineAudioInput::load`
// is that torque normalized. At 0.0 the shift is a constant-level click whatever
// the driver is doing, which is the tell of a synthesized gearbox. At 1.0 a shift
// made off the throttle is silent, which is wrong -- the dogs still move. 0.65
// leaves 35% of the level as the mechanism and puts 65% under the driver's foot.
inline constexpr double CLACK_LOAD_WEIGHT = 0.65;

// --- the clutch -----------------------------------------------------------------
//
// A KZ clutch is a dry centrifugal multiplate. It is loud when it slips, which on
// a shifter kart is exactly two places: pulling away, and any corner exit where
// the driver has dropped below the engagement speed. #83's wording is "clutch slip
// is audible during a launch" and that is the case being served.

// The slip at which the clutch layer is at full level, rad/s.
//
// **Derived**, from two numbers this repository already owns: `Clutch::lock_slip`
// is 2.0 rad/s and `clutch.h` records in its own comment that a launch runs "about
// 5 rad/s of speed per 240 Hz substep" and that the plates "slipped from a
// standing start all the way to the top of sixth". A real launch therefore spends
// its time between those and far above them -- the engine is at several hundred
// rad/s and the driveline at nearly zero. 300 rad/s is 2,865 rpm of difference at
// the crank, which is a heavy launch and not a corner-exit chirp, so the layer
// reaches full only when the clutch is genuinely being abused.
//
// Below `lock_slip` the layer is forced to silence outright rather than faded, so
// a locked clutch cannot hum. See `render`.
inline constexpr double CLUTCH_FULL_SLIP_RADS = 300.0;

// The clutch band, Hz.
//
// **`derived`; inputs estimated.** The free-free fundamental of a clutch steel
// plate -- the part that rings, because the friction discs are damped by their own
// lining. Section 3 of the physics header has the derivation and the sensitivity.
//
//     708 Hz at r 52 mm, t 1.5 mm
//
// Both inputs are estimates and both are anchored: the radius comes from the clutch
// bell's committed r 60 (`kart_spec` 30-powertrain, itself `estimated` from
// photographs, and that mark is inherited rather than laundered), and the thickness
// is an ordinary clutch steel. Across the plausible ranges the answer spans about
// 570-950 Hz.
//
// **This replaced an estimate of 520 Hz and the move is audible** -- 36% up, the
// one place tonight's derivation changed something a listener will notice. It also
// moves the layer *further* from `kz_audio::SCRUB_PEAK_HZ` at 1000 Hz, which was a
// real confusion risk since a launch spins the tires too.
//
// Listen for: too narrow and a launch sounds like a kettle.
inline double clutch_center_hz() {
	return plate_mode_hz(FREE_CIRCULAR_LAMBDA_2_0, CLUTCH_PLATE_THICKNESS_M,
			CLUTCH_PLATE_RADIUS_M, STEEL_E_PA, STEEL_RHO, STEEL_NU);
}

// How broad the clutch band is.
//
// **`derived` from the pack count, which is the one thing the plate count legitimately
// sets.** `clutch.h` records five friction discs and five steels: ten rubbing
// interfaces, ten uncorrelated stick-slip sources at different radii and clamp
// loads, exciting nominally identical plates whose tolerances spread their modes.
// Ten broadened sources summed is a shelf, not a peak. Q 0.8 is under critical
// damping, which is exactly that -- and using the plate count to compute a *centre*
// frequency instead would have been the laundering mistake the header warns about.
inline constexpr double CLUTCH_Q = 0.8;

// Shaping exponent on the normalized slip.
//
// **Estimated.** Below 1 the layer comes up fast and then flattens, which is what
// a friction surface does -- rubbing power is `torque * slip` and the torque a
// centrifugal clutch can hold saturates. 0.6 is inside that family and is a knob.
inline constexpr double CLUTCH_GAMMA = 0.6;

// --- both -----------------------------------------------------------------------

// PCG32 streams, distinct from `ScrubSynth`'s, `WindSynth`'s and `EngineSynth`'s
// three. ARCHITECTURE.md §8 rule 3: a shared generator makes what you hear depend
// on how many other things drew from it. Two more constants, drawn the same way
// the existing ones were.
inline constexpr uint64_t CLACK_STREAM = 0x9E3779B97F4A7C15ULL;
inline constexpr uint64_t CLUTCH_STREAM = 0xBF58476D1CE4E5B9ULL;

// How fast the clutch level is allowed to move, per sample at 48 kHz.
//
// The clack must NOT be smoothed -- it is an impact and smoothing an impact is
// removing it -- so this applies to the clutch layer alone. Same value and same
// argument as `scrub_wind_tuning::DRIVE_SMOOTHING_ALPHA`: `clutch_slip` is a
// solver output that can step between ticks, and a 35 ms one-pole keeps a tick's
// step from arriving as a click. **Unsourced**, and a smoothing constant rather
// than a spectrum, so no recording would upgrade it.
inline constexpr double CLUTCH_SMOOTHING_ALPHA = 0.0006;

} // namespace shift_tuning

// The knobs, as one struct, for the same reason `ScrubWindConfig` is one struct:
// the playback assembles it per block from atomics and hands it over, so a knob
// moving mid-corner is a coefficient step and not a click.
struct ShiftAudioConfig {
	// Layer gains, linear. Balance rather than level -- `EngineVoiceRig`'s bus
	// master owns the level, which is the split issue #160 asked for.
	//
	// **Set by measurement, not by ear, and the measurement is an audibility floor
	// rather than a preference.** Both started at 0.055 / 0.030, which were chosen
	// on the argument that a gearshift louder than the engine is worse than one too
	// quiet. An offline render through the real synths then said those values were
	// inaudible, which is a worse failure than either:
	//
	//     clack_gain 0.055   engagement transient +9.6 dB over its own background,
	//                        against +10.1 dB for the torque cut ALONE -- i.e. the
	//                        clack made the shift very slightly less prominent than
	//                        the engine dropping out already made it. The layer was
	//                        doing nothing a listener could attribute to it.
	//     clack_gain 0.20    +15.4 dB over background, stem peak -10.6 dBFS, which
	//                        is level with the engine note's own peak for the 9 ms
	//                        the impact lasts. Audible as an impact, gone before
	//                        drive returns.
	//
	//     clutch_gain 0.030  stem peak -24.1 dBFS, 14 dB under the engine note on a
	//                        launch -- present in a level meter and not in a room.
	//     clutch_gain 0.09   stem peak -14.5 dBFS, about 4 dB under the note.
	//
	// So these are still `estimated` and still owed an ear -- what changed is that
	// they are now estimates of *something*, with the sweep that produced them in
	// the report. #160's mixing pass owns where they finally land.
	double clack_gain = 0.20;
	double clutch_gain = 0.09;

	// The two bands are CALLED rather than copied, so a config built anywhere
	// carries the derivation and not a snapshot of it.
	double clack_center_hz = shift_tuning::clack_center_hz();
	double clack_q = shift_tuning::CLACK_Q;
	double clack_decay_fraction = shift_tuning::CLACK_DECAY_FRACTION;
	double clutch_center_hz = shift_tuning::clutch_center_hz();
	double clutch_q = shift_tuning::CLUTCH_Q;
	double clutch_full_slip_rads = shift_tuning::CLUTCH_FULL_SLIP_RADS;

	// `Gearbox::shift_time`, carried rather than included.
	//
	// **The solver owns this number and this struct is only told it**, which is why
	// it is a field with a default rather than a `#include "core/gearbox.h"`: the
	// gearbox is a tunable and a synth that compiled the default in would keep
	// playing 65 ms clacks after somebody moved it. `KartBody` publishes the live
	// value; the default here matches `Gearbox::shift_time` so a synth nobody tells
	// is right rather than zero.
	double shift_time_s = 0.065;
};

// The shift and clutch layer.
//
// Two sub-layers summed, and they are summed here rather than mounted separately
// because -- unlike scrub and wind -- both come from the same place on the kart at
// the same instant, and #160's "turn one off without re-judging the other" does
// not apply to two halves of one gearchange.
class ShiftSynth {
public:
	ShiftSynth() { configure(ShiftAudioConfig(), 48000.0); }

	// Not an audio-thread call: it resets the filters and reseeds both PRNGs.
	void configure(const ShiftAudioConfig &config, double sample_rate) {
		config_ = config;
		sample_rate_ = sample_rate > 1.0 ? sample_rate : 48000.0;
		reset();
	}

	// Audio-thread safe, same contract as `ScrubSynth::set_tuning`: copies scalars,
	// touches no filter state and no envelope.
	void set_tuning(const ShiftAudioConfig &config) { config_ = config; }
	void set_clack_gain(double g) { config_.clack_gain = g > 0.0 ? g : 0.0; }
	void set_clutch_gain(double g) { config_.clutch_gain = g > 0.0 ? g : 0.0; }
	double clack_gain() const { return config_.clack_gain; }
	double clutch_gain() const { return config_.clutch_gain; }

	void reset() {
		clack_filter_.reset();
		clutch_filter_.reset();
		clack_rng_.reseed(0, shift_tuning::CLACK_STREAM);
		clutch_rng_.reseed(0, shift_tuning::CLUTCH_STREAM);
		clack_env_ = 0.0;
		clutch_level_ = 0.0;
		was_shifting_ = false;
		gear_at_shift_start_ = 0;
		seen_ = false;
	}

	// Publish one tick of state. Physics thread.
	void publish(const EngineAudioInput &input) { input_ = input; }

	// Render `frames` mono samples. Audio thread. Allocates nothing.
	void render(float *out, int frames) {
		if (frames <= 0) {
			return;
		}

		// --- the edges, once per block -------------------------------------------
		//
		// **Edge detection lives here and not in the solver**, which is the same
		// placement argument `SurfaceWatcher` makes in `surface.h`: an event
		// detector that the simulation cannot read back keeps a replay identical
		// whether or not anything is listening (ARCHITECTURE.md §8).
		//
		// The block rate is 512 frames at 48 kHz = 10.7 ms and `shift_time` is
		// 65 ms, so a shift spans about six blocks and neither edge can be missed
		// by looking once per block. The alternative -- edge detection per sample
		// against a state that only changes per tick -- would be five thousand
		// redundant comparisons for the same answer.
		const bool shifting = input_.shifting;
		if (!seen_) {
			// The first block is not an edge. Spawning a kart mid-shift must not
			// fire a clack from nowhere -- exactly `SurfaceWatcher`'s first-sample
			// rule and for the same reason.
			seen_ = true;
			was_shifting_ = shifting;
			gear_at_shift_start_ = input_.gear;
		} else if (shifting && !was_shifting_) {
			// Rising edge: the dogs come out. Quiet.
			gear_at_shift_start_ = input_.gear;
			strike(shift_tuning::CLACK_DISENGAGE_GAIN, true);
			was_shifting_ = true;
		} else if (!shifting && was_shifting_) {
			// Falling edge: the dogs go in. This is the loud one, and it is the
			// only place the direction is known -- `drivetrain.h` publishes the OLD
			// gear for the whole shift and flips it exactly here.
			const bool up = input_.gear > gear_at_shift_start_;
			strike(shift_tuning::CLACK_ENGAGE_GAIN, up);
			was_shifting_ = false;
		}

		// --- coefficients, once per block ----------------------------------------
		clack_filter_.set(config_.clack_center_hz, config_.clack_q, sample_rate_);
		clutch_filter_.set(config_.clutch_center_hz, config_.clutch_q, sample_rate_);

		// The clack envelope's per-sample decay. A time constant expressed as a
		// fraction of the solver's own shift duration -- see CLACK_DECAY_FRACTION.
		const double tau = config_.clack_decay_fraction *
				(config_.shift_time_s > 1e-6 ? config_.shift_time_s : 1e-6);
		const double decay = std::exp(-1.0 / (tau * sample_rate_));

		// --- the clutch target ---------------------------------------------------
		//
		// Forced to zero below `Clutch::lock_slip`, not faded. A locked clutch is
		// not a quiet clutch, it is a silent one, and a fade would leave the layer
		// humming through every steady-state lap.
		double slip = input_.clutch_slip < 0.0 ? -input_.clutch_slip : input_.clutch_slip;
		double clutch_target = 0.0;
		if (slip > LOCK_SLIP_RADS) {
			const double full = config_.clutch_full_slip_rads > 1e-6
					? config_.clutch_full_slip_rads
					: 1e-6;
			double n = (slip - LOCK_SLIP_RADS) / full;
			n = n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
			// Normalized by what the chain does to white noise, so `clutch_gain` is
			// a level and not a level times whatever Q happens to be. Same fix and
			// same reason as `SCRUB_CHAIN_RMS_C` -- CLAUDE.md's entry on a filter
			// whose output level tracks its shape parameter. `band_chain_rms` below
			// is the shared form.
			clutch_target = std::pow(n, shift_tuning::CLUTCH_GAMMA) *
					config_.clutch_gain *
					band_normalize(config_.clutch_center_hz, config_.clutch_q);
		}

		const double alpha = smoothing_alpha(sample_rate_);
		const double clack_scale = clack_strike_ *
				band_normalize(config_.clack_center_hz, config_.clack_q) *
				config_.clack_gain;

		for (int i = 0; i < frames; ++i) {
			clutch_level_ += (clutch_target - clutch_level_) * alpha;

			double lp = 0.0;
			double bp = 0.0;
			clack_filter_.process(clack_rng_.next_double() * 2.0 - 1.0, lp, bp);
			const double clack = bp * clack_env_ * clack_scale;
			clack_env_ *= decay;

			double clp = 0.0;
			double cbp = 0.0;
			clutch_filter_.process(clutch_rng_.next_double() * 2.0 - 1.0, clp, cbp);
			const double clutch = cbp * clutch_level_;

			// The shared ceiling, same threshold and same shape as every other
			// layer. `scrub_wind.h`'s `soft_clip` is reused rather than a second
			// one written, because the three layers sum into one mixer and two
			// answers to "what is full scale" is how a mix ends up depending on
			// which layer is loudest at that instant.
			out[i] = static_cast<float>(soft_clip(clack + clutch));
		}
	}

	// For the tests and the gate: the clack envelope and the clutch level.
	//
	// Both are **gains applied to a filter output**, not the output's own level --
	// exactly the caveat `ScrubSynth::level()` carries, and named the same way so a
	// reader who has read one has read both.
	double clack_envelope() const { return clack_env_; }
	double clutch_level() const { return clutch_level_; }

	// How many clacks have been struck since `reset`. The gate's fingerprint: a
	// sabotage that stops shifts being detected has to show up as this not moving,
	// rather than as a level the probe could have got some other way.
	int64_t strikes() const { return strikes_; }

	// The RMS gain this file's band-pass applies to unit white noise, so a gain
	// constant here is comparable to every other gain constant in the project.
	//
	// **The Cytomic SVF's `bandpass` output has a peak gain of Q, not 1** --
	// CLAUDE.md's entry, and the reason `ScrubSynth` carries `SCRUB_CHAIN_RMS_C` at
	// all. Without dividing this back out, `clack_gain` and `clutch_gain` would be
	// "a level times whatever Q happens to be", the timbre knob would move the
	// loudness, and a driver adjusting one would have to re-set the other.
	//
	// **The first version of this function was the closed form and it was wrong by
	// up to 39%.** The ideal analog two-pole band-pass gives
	// `sqrt(pi * Q * f0 / (2 fs))` -- i.e. `1.2533 * Q^0.5 * sqrt(f0/fs)` -- and
	// `test_shift_audio.cpp` measured 1.240 / 1.331 / 1.374 / 1.394 times that at
	// Q 0.5 / 1.2 / 3 / 8. A residual that **climbs monotonically with the
	// parameter is a wrong exponent** and a flat offset is a wrong constant; this
	// one climbed and then flattened, so both were wrong. The TPT discretization's
	// frequency warping is the cause and it is not worth deriving in closed form.
	//
	// So this is a **fit to the measurement**, stated as one, exactly as
	// `SCRUB_CHAIN_RMS_C` and `SCRUB_CHAIN_RMS_Q_EXPONENT` are:
	//
	//     rms  =  1.368 * Q^0.541 * (f0/fs)^0.438
	//
	// **Both exponents are off theory and both were found the same way.** The Q
	// exponent went in first, at 0.541 against a theoretical 0.5, and the Q sweep
	// came back inside 1.8%. The f0 sweep was then added -- it had NOT been there,
	// because the fit was taken at one center frequency and `sqrt(f0/fs)` was
	// assumed rather than checked -- and it read 1.090 / 1.056 / 1.018 / 0.900 over
	// 300 / 900 / 2200 / 6000 Hz. **A monotonic decline is a wrong exponent**, and
	// 0.438 is the least-squares fit that removes it.
	//
	// That is the whole lesson worth carrying: a test that only sweeps the
	// parameter you fitted cannot tell a correct law from a coincidence at one
	// point, and the first version of this test swept exactly the parameter that
	// was already right.
	//
	// **Fitted over Q 0.5-8 and f0 300-6000 Hz at fs 48 kHz**, where the residual
	// is within 6%. The relationship is genuinely curved rather than a power law --
	// the TPT discretization warps frequency, and using `tan(pi f0/fs)` as the
	// variable instead makes it worse, measured -- so outside that range the
	// residual grows and this function is not to be trusted. Both shipped centers,
	// 520 and 2200 Hz, sit comfortably inside it, and over the +-1 octave a driver
	// would move either knob the fit is within 2%. `test_shift_audio.cpp` sweeps
	// both axes and fails past 8%.
	static constexpr double BAND_RMS_C = 1.368;
	static constexpr double BAND_RMS_Q_EXPONENT = 0.541;
	static constexpr double BAND_RMS_F0_EXPONENT = 0.438;

	static double band_chain_rms(double center_hz, double q, double sample_rate) {
		const double f = center_hz > 1.0 ? center_hz : 1.0;
		const double qq = q > 1e-3 ? q : 1e-3;
		const double fs = sample_rate > 1.0 ? sample_rate : 48000.0;
		return BAND_RMS_C * std::pow(qq, BAND_RMS_Q_EXPONENT) *
				std::pow(f / fs, BAND_RMS_F0_EXPONENT);
	}

private:
	// `Clutch::lock_slip`. Carried rather than included for the same reason
	// `shift_time_s` is a field: `clutch.h` owns it and a synth that reached into
	// the solver's headers would be a second reader of a tunable. This one is a
	// floor rather than a curve, so it is a constant here and not a config field --
	// moving it changes whether a *locked* clutch is silent, which is not a knob.
	static constexpr double LOCK_SLIP_RADS = 2.0;

	// Fire a clack. `up` scales it; `engage` selects which of the two impacts.
	void strike(double impact_gain, bool up) {
		// Load-weighted, so a shift made off the throttle is quieter than one made
		// against full drive but is never silent. See CLACK_LOAD_WEIGHT.
		double load = input_.load;
		load = load < 0.0 ? 0.0 : (load > 1.0 ? 1.0 : load);
		const double w = shift_tuning::CLACK_LOAD_WEIGHT;
		const double drive = (1.0 - w) + w * load;
		const double dir = up ? 1.0 : shift_tuning::CLACK_DOWNSHIFT_SCALE;
		// **Set, not added.** A second shift arriving before the first has decayed
		// restarts the envelope rather than stacking on it: stacking is how a
		// downshift chain through three gears produces one enormous bang instead of
		// three knocks.
		clack_env_ = 1.0;
		clack_strike_ = impact_gain * dir * drive;
		++strikes_;
	}

	double band_normalize(double center_hz, double q) const {
		const double rms = band_chain_rms(center_hz, q, sample_rate_);
		return rms > 1e-9 ? 1.0 / rms : 1.0;
	}

	static double smoothing_alpha(double sample_rate) {
		// Declared at 48 kHz; at another rate the same time constant is a different
		// per-sample fraction. Same scaling and same argument as
		// `ScrubSynth::drive_alpha`.
		const double a = shift_tuning::CLUTCH_SMOOTHING_ALPHA * 48000.0 / sample_rate;
		return a > 1.0 ? 1.0 : a;
	}

	ShiftAudioConfig config_{};
	EngineAudioInput input_{};
	Svf clack_filter_{};
	Svf clutch_filter_{};
	Pcg32 clack_rng_{ 0, shift_tuning::CLACK_STREAM };
	Pcg32 clutch_rng_{ 0, shift_tuning::CLUTCH_STREAM };
	double sample_rate_ = 48000.0;
	double clack_env_ = 0.0;
	double clack_strike_ = 0.0;
	double clutch_level_ = 0.0;
	bool was_shifting_ = false;
	bool seen_ = false;
	int gear_at_shift_start_ = 0;
	int64_t strikes_ = 0;
};

} // namespace kart::core

#endif // KART_CORE_SHIFT_AUDIO_H
