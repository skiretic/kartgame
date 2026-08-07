#ifndef KART_CORE_ROLL_AUDIO_H
#define KART_CORE_ROLL_AUDIO_H

#include "core/audio_state.h"
#include "core/pcg32.h"
#include "core/surface.h"
// For `Svf` and `soft_clip`. Same borrow and same reason as `shift_audio.h`.
#include "core/scrub_wind.h"

#include <cmath>
#include <cstdint>

// Surface-dependent rolling noise. Issue #85, ARCHITECTURE.md §12's third audio
// layer, and the one `noise_voice.h`'s `Layer` enum has been anticipating in its
// own comment since it was written.
//
// Nothing here may include godot-cpp. ADR-0017.
//
// ================================================================================
// ROLLING IS NOT SCRUB, AND CONFUSING THEM WOULD MAKE BOTH WRONG
// ================================================================================
//
// `ScrubSynth` is driven by **slip** and is the sound of a tire failing. This is
// driven by **speed** and is the sound of a tire working. They are separable in
// the solver -- `EngineAudioInput` carries `scrub` and `speed_ms` as different
// fields -- and separable at the mixer, and the reason it matters is that a kart
// spends almost all of its lap rolling and almost none of it sliding. A rolling
// layer folded into the scrub layer would be audible constantly and would make the
// scrub layer's own measured band a lie.
//
// ================================================================================
// WHAT IS SOURCED AND WHAT IS NOT
// ================================================================================
//
// ## Sourced, and this is the good one
//
// **A curb is periodic, and its frequency is arithmetic.** `surface.h` carries
// `ripple_wavelength = 0.15 m` and `ripple_amplitude = 0.012 m` for
// `SURFACE_CURB`, as the definition M5's curb mesh is generated from -- one owner,
// and the collider a wheel actually rides agrees with it by construction. A wheel
// crossing a periodic profile of wavelength `lambda` at road speed `v` is excited
// at `f = v / lambda` Hz. That is not a texture and not a guess; it is the same
// number twice.
//
//     at  10 m/s   0.15 m ->   66.7 Hz
//     at  20 m/s              133.3 Hz
//     at  30 m/s              200.0 Hz
//
// `test_roll_audio.cpp` measures that frequency back out of the rendered signal
// rather than restating it, which is the only way this claim is worth anything.
//
// **The wavelength itself is `estimated` upstream and says so.** `surface.h`'s
// comment is explicit: the CIK-FIA circuit regulations specify a kerb's paint and
// nothing about its profile, and 0.15 m is anchored to the tire -- a rear slick of
// 0.1475 m radius has a 49 mm contact chord and 0.15 m is three chords, the
// shortest ripple a kart can still feel individually. So the *law* here is derived
// and exact, and the *constant it is derived from* is an estimate with its
// reasoning attached, one file upstream, where it belongs. This file does not get a
// second copy of it -- it reads `Surface::ripple_wavelength`.
//
// **`Surface::roughness` exists for this file by name.** Its comment in
// `surface.h` says so in as many words: "the mix parameter §12's tire-scrub and
// rolling audio and M10's particle emitters take". asphalt 0.25, curb 0.35,
// grass 0.80, dirt 0.90.
//
// ## Estimated: the level and the spectrum
//
// **No rolling-noise recording was measured for this.** The scrub corpus
// (`tools/assets/fetch_scrub_audio.sh`) is four recordings of tires *squealing*,
// which is the other layer; nothing in it is a tire rolling at a known speed on a
// known surface with the engine separable. Every level and spectral constant below
// is therefore `estimated` and each carries its anchor. The anchors are internal
// consistency rather than nothing, and they are named.
//
// The most important of them: **the speed law is anchored to the wind layer's,
// which IS sourced.** `kz_audio::WIND_DB_PER_SPEED_DOUBLING` is 18.0 dB, measured.
// Aerodynamic noise power goes as a high power of speed and tire/road noise does
// not -- rolling is contact excitation, whose amplitude grows with how fast the
// tread is being deformed rather than with the cube of anything. So the rolling
// exponent has to be **materially below 18**, and the constant below says which
// number was chosen and what it would sound like if it were wrong. If rolling and
// wind shared an exponent the two layers would be indistinguishable at every
// speed, which is a check anyone can run by ear and `audio.sh` runs numerically.

namespace kart::core {

namespace roll_tuning {

// dB per doubling of road speed for the rolling layer.
//
// **Estimated**, anchored to a sourced number it must not equal. See the header:
// `kz_audio::WIND_DB_PER_SPEED_DOUBLING` is 18.0 and is measured; rolling must sit
// materially below it or the two layers are one layer. 9.0 is half of it, which
// makes rolling dominant at low speed and wind dominant at high speed with a
// crossover in the middle of the kart's range -- the behavior that lets a driver
// hear speed at all when leaving a hairpin, which is the entire point of the layer.
//
// Listen for: too high and the layer disappears in slow corners and roars on the
// straight, doubling the wind; too low and it is a constant hiss that never
// changes and reads as a broken loop.
inline constexpr double ROLL_DB_PER_SPEED_DOUBLING = 9.0;

// The speed the level law is referenced to, m/s.
//
// **Derived**, and deliberately the same anchor the wind layer uses:
// `scrub_wind_tuning::WIND_REFERENCE_SPEED_MS` is 38.0, which is about the kart's
// measured 139.8 km/h top speed. Referencing both layers to the same speed is what
// makes their two gain constants directly comparable -- at 38 m/s each realizes
// its own gain, so the ratio of the two numbers *is* the balance between them.
inline constexpr double ROLL_REFERENCE_SPEED_MS = 38.0;

// The rolling band's corner at the reference speed, Hz, and how it moves.
//
// **Estimated.** Rolling noise is low and broad -- it is a large contact patch
// deforming, not a small feature resonating -- so this is a low-pass corner and not
// a band center, which is the structural difference from `ScrubSynth`. 900 Hz at
// 38 m/s puts most of the energy under the scrub band's measured 1000 Hz peak, so
// the two layers occupy different places even when both are running, which they
// are through every corner.
//
// The corner rises with speed because the excitation does: a tread element is
// deformed and released once per contact-patch length, and that rate is
// proportional to speed. Proportional and not fixed, therefore, and the floor keeps
// a stationary kart from having a 0 Hz filter.
inline constexpr double ROLL_CUTOFF_AT_REFERENCE_HZ = 900.0;
inline constexpr double ROLL_CUTOFF_FLOOR_HZ = 60.0;
inline constexpr double ROLL_FILTER_Q = 0.707;

// How much brighter a rough surface is than a smooth one, as a multiplier on the
// low-pass corner at `roughness = 1`.
//
// **Estimated.** Loose material is small hard particles against rubber and reads
// far brighter than asphalt; 2.2 at roughness 1.0, interpolated linearly from 1.0
// at roughness 0, puts dirt (0.90) at 2.08x asphalt's corner and curb (0.35) at
// 1.42x. Listen for: too high and grass sounds like static; too low and every
// surface sounds the same, which is the failure this whole layer exists to avoid.
inline constexpr double ROLL_ROUGH_BRIGHTNESS = 2.2;

// How much louder a rough surface is, as a multiplier at `roughness = 1`.
//
// **Estimated.** Separate from brightness on purpose: grass is both louder and
// duller than gravel, so one parameter could not express the table. 2.6 puts grass
// 6.5 dB above asphalt at the same speed, which is roughly the step a driver
// needs to notice they have left the circuit without looking.
inline constexpr double ROLL_ROUGH_LOUDNESS = 2.6;

// --- the curb ---------------------------------------------------------------
//
// The one part of this file that is derived rather than estimated.

// How much of the curb's level is the periodic component, 0..1.
//
// **Estimated**, but bounded by something real: a curb is a rectified cosine
// standing on a flat top face (`surface::ripple_height`), so a wheel riding it is
// modulated between full contact and reduced contact rather than leaving the
// ground -- 12 mm of amplitude against a tire that deflects about 2 mm means the
// wheel follows the profile and is never unloaded to zero. A modulation depth of
// 1.0 would be the wheel leaving the surface entirely once per ripple, which is
// not what the geometry says. 0.75 is deep enough to be unmistakable and short of
// the physically wrong end.
inline constexpr double CURB_MODULATION_DEPTH = 0.75;

// The highest rumble rate that is still a rumble, Hz.
//
// **Derived, from the tire rather than chosen.** `surface.h`'s own argument for the
// 0.15 m wavelength is that a tire bridges any ripple much shorter than its 49 mm
// contact chord. Turn that around: above the rate at which the contact patch spans
// a whole ripple, the wheel stops resolving individual bumps and the modulation
// physically stops. That happens at `v / lambda` where `v` is the speed at which
// the chord equals the wavelength -- but the chord is fixed and the wavelength is
// fixed, so the honest statement is that the modulation is real at every speed the
// kart reaches and this ceiling exists only to keep the modulator from entering the
// audio band and becoming a tone. At 0.15 m the kart would need 45 m/s (162 km/h)
// to reach 300 Hz, above its measured 139.8 km/h top speed, so this ceiling is
// **unreachable in normal driving** and is a guard rather than a shaping constant.
// That it is unreachable is the point: a shaping constant that never binds cannot
// quietly become the sound.
inline constexpr double CURB_RUMBLE_CEILING_HZ = 300.0;

// PCG32 stream, distinct from the other five in this project.
inline constexpr uint64_t ROLL_STREAM = 0x94D049BB133111EBULL;

// Level smoothing, per sample at 48 kHz. Same value and same argument as
// `scrub_wind_tuning::DRIVE_SMOOTHING_ALPHA` -- **unsourced**, a time constant
// rather than a spectrum. A wheel crossing a surface boundary steps `surface`
// between one tick and the next, and 35 ms is what keeps that from being a click.
inline constexpr double ROLL_SMOOTHING_ALPHA = 0.0006;

} // namespace roll_tuning

struct RollAudioConfig {
	// Layer gain, linear. Balance, not level -- the bus master owns the level.
	//
	// **Set against a stated audibility criterion, and the criterion is the honest
	// part.** This layer exists so a driver can hear which surface they are on. The
	// acceptance number is therefore the LEVEL SPREAD across the four surfaces at
	// one speed, measured offline through the real synths, four surfaces at a
	// constant 20 m/s with a constant engine state so the only variable is the
	// surface:
	//
	//     roll_gain   spread across four surfaces   spectral centroid spread
	//     0.022                0.58 dB                     939 Hz
	//     0.05                 2.09 dB                    1736 Hz
	//     0.09                 3.94 dB                    2168 Hz
	//     0.15                 5.43 dB                    2227 Hz
	//     0.25                 6.34 dB                    1945 Hz
	//
	// Before this layer existed the spread was **0.03 dB and 81 Hz** -- the four
	// surfaces were the same sound, because the only surface-dependent layer was
	// scrub and a kart rolling straight has no slip, so nothing in the mix read the
	// surface at all. 0.09 is the smallest value that puts grass and dirt clearly
	// above asphalt (+5.5 and +5.9 dB against +2.9) without the layer dominating.
	//
	// **This is a game-feel decision and not a physical one, and it must not be
	// dressed as physics.** A KZ at 11,000 rpm is deafening and a real driver cannot
	// hear their own tires rolling at all; the physically faithful gain for this
	// layer is roughly zero. It is here because the driver needs the cue, which is
	// the same reason the layer was specified. `estimated`, and the reasoning is
	// that sentence rather than a measurement of a tire.
	//
	// It costs headroom and the report says how much: it is the loudest of the two
	// new layers in the steady state, and the peak of the shift scene moved
	// -11.45 -> -7.82 dBFS with it in.
	double roll_gain = 0.09;

	double db_per_speed_doubling = roll_tuning::ROLL_DB_PER_SPEED_DOUBLING;
	double cutoff_at_reference_hz = roll_tuning::ROLL_CUTOFF_AT_REFERENCE_HZ;
	double rough_brightness = roll_tuning::ROLL_ROUGH_BRIGHTNESS;
	double rough_loudness = roll_tuning::ROLL_ROUGH_LOUDNESS;
	double curb_modulation_depth = roll_tuning::CURB_MODULATION_DEPTH;
};

// Surface-dependent rolling noise, with the curb's periodic modulation.
class RollSynth {
public:
	RollSynth() { configure(RollAudioConfig(), 48000.0); }

	// Not an audio-thread call: resets the filter and reseeds the PRNG.
	void configure(const RollAudioConfig &config, double sample_rate) {
		config_ = config;
		sample_rate_ = sample_rate > 1.0 ? sample_rate : 48000.0;
		reset();
	}

	// Audio-thread safe: copies scalars, touches no filter state.
	void set_tuning(const RollAudioConfig &config) { config_ = config; }
	void set_gain(double g) { config_.roll_gain = g > 0.0 ? g : 0.0; }
	double gain() const { return config_.roll_gain; }

	void reset() {
		filter_.reset();
		rng_.reseed(0, roll_tuning::ROLL_STREAM);
		level_ = 0.0;
		phase_ = 0.0;
	}

	void publish(const EngineAudioInput &input) { input_ = input; }

	void render(float *out, int frames) {
		if (frames <= 0) {
			return;
		}

		const Surface &surf = surface(input_.surface);
		const double rough = surf.roughness < 0.0 ? 0.0
													: (surf.roughness > 1.0 ? 1.0 : surf.roughness);
		double speed = input_.speed_ms;
		speed = speed < 0.0 ? -speed : speed;

		// --- the filter, once per block ------------------------------------------
		//
		// Corner proportional to speed and scaled by the surface's brightness. The
		// floor keeps a stationary kart off a 0 Hz filter, where `Svf::set` clamps
		// anyway -- belt and braces, because a clamp is a silent correction and a
		// named floor is a readable one.
		const double bright = 1.0 + rough * (config_.rough_brightness - 1.0);
		double cutoff = config_.cutoff_at_reference_hz * bright *
				(speed / roll_tuning::ROLL_REFERENCE_SPEED_MS);
		if (cutoff < roll_tuning::ROLL_CUTOFF_FLOOR_HZ) {
			cutoff = roll_tuning::ROLL_CUTOFF_FLOOR_HZ;
		}
		const double nyquist_guard = sample_rate_ * 0.45;
		if (cutoff > nyquist_guard) {
			cutoff = nyquist_guard;
		}
		filter_.set(cutoff, roll_tuning::ROLL_FILTER_Q, sample_rate_);

		// --- the level -----------------------------------------------------------
		//
		// A dB-per-doubling law, converted to an amplitude exponent by the same
		// helper the wind layer uses so the two are stated in the same units and a
		// reader can compare 9.0 against 18.0 directly. `20*log10(2^n) = n*6.0206`.
		const double exponent = scrub_wind_tuning::wind_exponent_for_db_per_doubling(
				config_.db_per_speed_doubling);
		const double ratio = speed / roll_tuning::ROLL_REFERENCE_SPEED_MS;
		// A kart at rest rolls silently, and `pow(0, n)` is 0, which is correct --
		// but only if nothing below re-adds a floor. Nothing does.
		const double speed_gain = ratio > 0.0 ? std::pow(ratio, exponent) : 0.0;
		const double loud = 1.0 + rough * (config_.rough_loudness - 1.0);

		// Divided by what the chain does to white noise, so `roll_gain` is a level
		// and not a level times whatever the cutoff happens to be. This is the
		// defect that shipped twice in `scrub_wind.h` -- once for scrub and once for
		// wind, a commit apart -- and it is the reason the wind layer realized
		// 20.49 dB per doubling against a law that says 18.0. Without this line the
		// same 2.49 dB would arrive here from the same place, and the layer's
		// exponent would not be the exponent written on it.
		const double chain = scrub_wind_tuning::wind_chain_rms(cutoff, sample_rate_);
		const double normalize = chain > 1e-9 ? 1.0 / chain : 1.0;

		const double target = speed_gain * loud * config_.roll_gain * normalize;

		// --- the curb rumble -----------------------------------------------------
		//
		// `f = v / lambda`, and lambda is read off the surface row rather than
		// copied here. Zero on every surface with no periodic structure, which is
		// every surface but the curb -- and `ripple_wavelength` being 0.0 there is
		// what makes that fall out rather than needing a branch on the enum.
		double rumble_hz = 0.0;
		if (surf.ripple_wavelength > 1e-6) {
			rumble_hz = speed / surf.ripple_wavelength;
			if (rumble_hz > roll_tuning::CURB_RUMBLE_CEILING_HZ) {
				rumble_hz = roll_tuning::CURB_RUMBLE_CEILING_HZ;
			}
		}
		const double phase_step = 2.0 * 3.14159265358979323846 * rumble_hz / sample_rate_;
		const double depth = rumble_hz > 0.0 ? clamp01(config_.curb_modulation_depth) : 0.0;

		const double alpha = smoothing_alpha(sample_rate_);

		for (int i = 0; i < frames; ++i) {
			level_ += (target - level_) * alpha;

			double lp = 0.0;
			double bp = 0.0;
			filter_.process(rng_.next_double() * 2.0 - 1.0, lp, bp);

			// The modulator. A rectified cosine, matching `surface::ripple_height`'s
			// own shape rather than a sine -- the profile is a row of bumps standing
			// on a flat face, so the excitation is zero at its minimum and never
			// negative. Using a sine here would put a groove in the concrete for
			// half of every cycle, which is the same mistake `surface.h` argues
			// against in the geometry.
			double amp = 1.0;
			if (depth > 0.0) {
				const double bump = 0.5 * (1.0 - std::cos(phase_));
				amp = (1.0 - depth) + depth * bump;
				phase_ += phase_step;
				if (phase_ > 2.0 * 3.14159265358979323846) {
					phase_ -= 2.0 * 3.14159265358979323846;
				}
			}

			out[i] = static_cast<float>(soft_clip(lp * level_ * amp));
		}
	}

	// The gain the layer settled at. Same caveat as `ScrubSynth::level()`: it is
	// the multiplier applied to the filter output and carries the chain
	// normalization, so it is not bounded by 1 and is not the output's own level.
	double level() const { return level_; }

	// The rumble rate the layer would run at for a given speed and surface, Hz.
	//
	// Exposed so the gate can compare it against `v / lambda` computed
	// independently, rather than against a number this class printed about itself.
	static double rumble_hz_for(int surface_type, double speed_ms) {
		const Surface &row = surface(surface_type);
		if (row.ripple_wavelength <= 1e-6) {
			return 0.0;
		}
		const double f = (speed_ms < 0.0 ? -speed_ms : speed_ms) / row.ripple_wavelength;
		return f > roll_tuning::CURB_RUMBLE_CEILING_HZ ? roll_tuning::CURB_RUMBLE_CEILING_HZ : f;
	}

private:
	static double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

	static double smoothing_alpha(double sample_rate) {
		const double a = roll_tuning::ROLL_SMOOTHING_ALPHA * 48000.0 / sample_rate;
		return a > 1.0 ? 1.0 : a;
	}

	RollAudioConfig config_{};
	EngineAudioInput input_{};
	Svf filter_{};
	Pcg32 rng_{ 0, roll_tuning::ROLL_STREAM };
	double sample_rate_ = 48000.0;
	double level_ = 0.0;
	double phase_ = 0.0;
};

} // namespace kart::core

#endif // KART_CORE_ROLL_AUDIO_H
