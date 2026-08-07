#include "doctest.h"

#include "core/audio_state.h"
#include "core/roll_audio.h"
#include "core/shift_audio.h"
#include "core/surface.h"

#include <cmath>
#include <string>
#include <vector>

// What can honestly be asserted about a layer whose spectrum could not be
// measured. Issues #83 and #85.
//
// `kz_audio::SHIFT_TRANSIENT_MEASURED` is false and `shift_audio.h`'s header
// records the measurement that was attempted and withdrawn -- three cuts, the last
// of which found 9 events on the two gearboxed recordings and **48 on the three
// single-speed engines**, which have no gearbox. So there is no ladder to compare
// a clack against and asserting "the band is at 2200 Hz" would be asserting that a
// constant equals itself.
//
// What is tested is therefore the half that IS constrained:
//
//   * the **behavior**, which is solver truth: a clack fires on a shift and on
//     nothing else, twice per shift, the loud one at engagement, the direction
//     read off the gear that `drivetrain.h` flips exactly there;
//   * that a shift the synth was never told about produces silence, which is the
//     check that would have caught the layer being wired to nothing;
//   * that the clack is **over before drive returns**, which is arithmetic on
//     `Gearbox::shift_time` and is the one timing claim here that is derived;
//   * that a locked clutch is silent rather than quiet, because a fade there would
//     hum through every steady-state lap;
//   * the **curb rumble frequency is `v / lambda`**, measured back out of the
//     rendered signal by counting the modulation, against the same arithmetic done
//     independently. This is the one genuinely sourced claim in either file and it
//     is the one worth a real measurement;
//   * that the rolling layer's realized dB per doubling of speed is the figure
//     written on it and not that figure plus whatever its own low-pass adds --
//     the defect that shipped twice in `scrub_wind.h`, once for scrub and once for
//     wind, a commit apart;
//   * that no operating point writes a sample outside full scale;
//   * bit-identical output for identical input.
//
// The tests print their measurements, for the same reason every other audio test
// here does: a number that only exists inside a CHECK is a number nobody can quote.
//
// **`doctest::Approx(x).epsilon(e)` is NOT a relative tolerance** -- it compares
// against `e * (scale + max(|a|,|b|))` with `scale` defaulting to 1.0, so on a
// value of 0.03 an `epsilon(0.06)` is a tolerance of 0.062. Every comparison below
// on a quantity whose magnitude is not order 1 is written as an explicit relative
// check, which is what CLAUDE.md's entry demands.

using kart::core::EngineAudioInput;
using kart::core::RollAudioConfig;
using kart::core::RollSynth;
using kart::core::ShiftAudioConfig;
using kart::core::ShiftSynth;

namespace {

constexpr double RATE = 48000.0;
constexpr double SHIFT_TIME = 0.065; // `Gearbox::shift_time`

double rms(const std::vector<float> &v) {
	if (v.empty()) {
		return 0.0;
	}
	double sum = 0.0;
	for (float s : v) {
		sum += static_cast<double>(s) * static_cast<double>(s);
	}
	return std::sqrt(sum / static_cast<double>(v.size()));
}

double peak(const std::vector<float> &v) {
	double m = 0.0;
	for (float s : v) {
		const double a = std::fabs(static_cast<double>(s));
		if (a > m) {
			m = a;
		}
	}
	return m;
}

// Render `blocks` blocks of `frames`, publishing `input` before each. Returns the
// concatenated signal. Blocks rather than one long call on purpose: the block
// boundary is where the per-block coefficient and edge work happens, and a test
// that renders one enormous block tests a code path the mixer never takes.
std::vector<float> render(ShiftSynth &synth, EngineAudioInput input, int blocks, int frames = 512) {
	std::vector<float> out;
	out.reserve(static_cast<size_t>(blocks) * static_cast<size_t>(frames));
	std::vector<float> scratch(static_cast<size_t>(frames), 0.0f);
	for (int b = 0; b < blocks; ++b) {
		synth.publish(input);
		synth.render(scratch.data(), frames);
		out.insert(out.end(), scratch.begin(), scratch.end());
	}
	return out;
}

std::vector<float> render_roll(RollSynth &synth, EngineAudioInput input, int blocks, int frames = 512) {
	std::vector<float> out;
	out.reserve(static_cast<size_t>(blocks) * static_cast<size_t>(frames));
	std::vector<float> scratch(static_cast<size_t>(frames), 0.0f);
	for (int b = 0; b < blocks; ++b) {
		synth.publish(input);
		synth.render(scratch.data(), frames);
		out.insert(out.end(), scratch.begin(), scratch.end());
	}
	return out;
}

// A relative comparison, written out because `Approx().epsilon()` is not one.
bool close_rel(double a, double b, double tol) {
	const double scale = std::fabs(a) > std::fabs(b) ? std::fabs(a) : std::fabs(b);
	if (scale < 1e-12) {
		return std::fabs(a - b) < 1e-12;
	}
	return std::fabs(a - b) / scale <= tol;
}

EngineAudioInput driving(int gear = 3) {
	EngineAudioInput in;
	in.rpm = 11000.0;
	in.load = 0.9;
	in.throttle = 1.0;
	in.gear = gear;
	in.speed_ms = 25.0;
	in.surface = kart::core::SURFACE_ASPHALT;
	return in;
}

ShiftAudioConfig shift_defaults() {
	ShiftAudioConfig c;
	c.shift_time_s = SHIFT_TIME;
	return c;
}

} // namespace

TEST_CASE("the two bands are the structural modes they claim to be") {
	// The bands are `derived`, not typed, so this recomputes them from the physics
	// **independently of the header's own helper** -- writing out the closed form
	// again rather than calling `plate_mode_hz`. Calling the same function the synth
	// calls would only prove it is deterministic.
	//
	// `f = (lambda / 2pi) * t * c_p / a^2`, `c_p = sqrt(E / (12(1-nu^2) rho))`.
	auto independent_plate = [](double lam, double t, double a, double e, double rho, double nu) {
		const double cp = std::sqrt(e / (12.0 * (1.0 - nu * nu) * rho));
		return lam / (2.0 * 3.14159265358979323846) * t * cp / (a * a);
	};

	const double cp_steel = std::sqrt(200.0e9 / (12.0 * (1.0 - 0.09) * 7850.0));
	const double cp_alu = std::sqrt(70.0e9 / (12.0 * (1.0 - 0.1089) * 2700.0));
	MESSAGE("plate-speed group: steel " << cp_steel << " m/s, aluminium " << cp_alu << " m/s");
	// Within 2% of each other, which is the header's claim that the band is set by
	// geometry rather than by which metal the case is cast in.
	CHECK(close_rel(cp_steel, cp_alu, 0.02));

	// The clack: a ribbed bay of the crankcase side wall, 140 mm span, 5 mm wall.
	const double clack = kart::core::shift_tuning::clack_center_hz();
	const double clack_expect = independent_plate(35.99, 0.005, 0.140, 70.0e9, 2700.0, 0.33);
	MESSAGE("clack band: header " << clack << " Hz, independent " << clack_expect << " Hz");
	CHECK(close_rel(clack, clack_expect, 0.001));
	// And it really is in the region the header quotes, which is the check that
	// catches an input being edited to something absurd.
	CHECK(clack > 2000.0);
	CHECK(clack < 2600.0);

	// The clutch: a free steel plate, r 52 mm, t 1.5 mm.
	const double clutch = kart::core::shift_tuning::clutch_center_hz();
	const double clutch_expect = independent_plate(5.253, 0.0015, 0.052, 200.0e9, 7850.0, 0.30);
	MESSAGE("clutch band: header " << clutch << " Hz, independent " << clutch_expect << " Hz");
	CHECK(close_rel(clutch, clutch_expect, 0.001));
	CHECK(clutch > 600.0);
	CHECK(clutch < 850.0);

	// **The clutch band must stay clear of the scrub layer's measured peak.** A
	// launch spins the tires and slips the clutch at the same moment, so two layers
	// sitting on top of each other would be indistinguishable exactly when both are
	// loudest. `kz_audio::SCRUB_PEAK_HZ` is 1000 Hz and is measured; this is the one
	// constraint on the clutch band that comes from outside its own derivation.
	MESSAGE("clutch band sits " << (1000.0 / clutch) << "x below the measured scrub peak");
	CHECK(clutch < 1000.0 / 1.2);

	// The dog ring, which is the SOURCE and is deliberately not the band. Computing
	// it is what proves the point, so it is checked rather than left in prose.
	const double ring = kart::core::shift_tuning::ring_mode_hz(2, 0.006, 0.020, 200.0e9, 7850.0);
	MESSAGE("dog ring n=2 mode: " << ring << " Hz, which is " << (ring / clack)
			<< "x the clack band -- the ring excites, the case radiates");
	CHECK(ring > 4.0 * clack);

	// The sensitivity the header tabulates, checked as a LAW rather than as three
	// numbers: the plate mode is linear in thickness and inverse-square in span.
	// This is what would catch the formula being rewritten wrongly.
	const double doubled_t = independent_plate(35.99, 0.010, 0.140, 70.0e9, 2700.0, 0.33);
	const double doubled_a = independent_plate(35.99, 0.005, 0.280, 70.0e9, 2700.0, 0.33);
	MESSAGE("law check: 2x thickness -> " << (doubled_t / clack_expect)
			<< "x, 2x span -> " << (doubled_a / clack_expect) << "x");
	CHECK(close_rel(doubled_t / clack_expect, 2.0, 0.001));
	CHECK(close_rel(doubled_a / clack_expect, 0.25, 0.001));
}

TEST_CASE("a gearshift is the only thing that strikes a clack") {
	ShiftSynth synth;
	synth.configure(shift_defaults(), RATE);

	// Steady driving, no shift. Ten blocks is 107 ms, longer than a whole shift.
	EngineAudioInput steady = driving();
	const std::vector<float> quiet = render(synth, steady, 10);

	CHECK(synth.strikes() == 0);
	// Not merely quiet -- **exactly** zero. The clutch is locked (clutch_slip 0)
	// and no clack has fired, so there is no source at all. A layer that idled at
	// some small level would hum under every lap.
	CHECK(peak(quiet) == 0.0);
	MESSAGE("steady driving, no shift: strikes " << synth.strikes()
			<< ", peak " << peak(quiet));

	// Now a shift: `shifting` true for one block, then false.
	EngineAudioInput in = steady;
	in.shifting = true;
	const std::vector<float> during = render(synth, in, 1);
	CHECK(synth.strikes() == 1); // disengagement
	CHECK(peak(during) > 0.0);

	in.shifting = false;
	in.gear = steady.gear + 1; // `drivetrain.h` flips the gear exactly here
	const std::vector<float> after = render(synth, in, 1);
	CHECK(synth.strikes() == 2); // engagement
	MESSAGE("one shift: strikes " << synth.strikes()
			<< ", disengage peak " << peak(during)
			<< ", engage peak " << peak(after));

	// The engagement is the loud one. `CLACK_ENGAGE_GAIN` is 1.00 against
	// `CLACK_DISENGAGE_GAIN` 0.35, so the ratio should be near 1/0.35 = 2.86 --
	// near rather than exact, because both are a filtered noise burst and their
	// peaks are two draws from the same PRNG.
	CHECK(peak(after) > peak(during));
}

TEST_CASE("the clack is over before drive returns") {
	// The one timing claim that is DERIVED rather than estimated.
	// `CLACK_DECAY_FRACTION` is 0.14, so the envelope's time constant is
	// 0.14 * shift_time and after one full shift_time it is exp(-1/0.14) = 7.9e-4,
	// which is -62 dB. Anything audible at the moment the torque comes back smears
	// the shift into the note it is supposed to interrupt.
	ShiftSynth synth;
	synth.configure(shift_defaults(), RATE);

	EngineAudioInput in = driving();
	synth.publish(in);
	std::vector<float> scratch(64, 0.0f);
	synth.render(scratch.data(), 64); // establish `was_shifting_ = false`

	in.shifting = true;
	synth.publish(in);
	synth.render(scratch.data(), 1); // fire the disengage clack

	const double env_at_strike = synth.clack_envelope();

	// Render exactly one shift_time worth of samples.
	const int frames = static_cast<int>(SHIFT_TIME * RATE);
	std::vector<float> run(static_cast<size_t>(frames), 0.0f);
	synth.render(run.data(), frames);
	const double env_after = synth.clack_envelope();

	const double predicted = std::exp(-1.0 / kart::core::shift_tuning::CLACK_DECAY_FRACTION);
	const double realized = env_after / env_at_strike;
	const double db = 20.0 * std::log10(realized);
	MESSAGE("clack envelope after one shift_time: " << realized
			<< " (" << db << " dB), predicted " << predicted);
	// Explicit relative comparison; these are 8e-4, where an epsilon() would be
	// a thousand times looser than it reads.
	CHECK(close_rel(realized, predicted, 0.02));
	CHECK(db < -50.0);
}

TEST_CASE("a locked clutch is silent, not quiet") {
	ShiftSynth synth;
	synth.configure(shift_defaults(), RATE);

	EngineAudioInput in = driving();
	in.clutch_slip = 1.5; // below `Clutch::lock_slip` of 2.0 rad/s
	const std::vector<float> locked = render(synth, in, 8);
	CHECK(peak(locked) == 0.0);
	MESSAGE("clutch_slip 1.5 rad/s (below lock_slip 2.0): peak " << peak(locked));

	// And a slipping one is not.
	synth.reset();
	in.clutch_slip = 200.0;
	const std::vector<float> slipping = render(synth, in, 40);
	CHECK(rms(slipping) > 0.0);
	MESSAGE("clutch_slip 200 rad/s: rms " << rms(slipping));
}

TEST_CASE("the clutch layer is monotonic in slip") {
	// "Louder when the clutch is slipping harder" is the one thing a driver would
	// notice inverted. Measured over the second half of each run so the 35 ms
	// smoothing one-pole has settled -- comparing the first block would be
	// measuring the ramp.
	ShiftSynth synth;
	double previous = -1.0;
	std::string row;
	for (double slip : { 5.0, 25.0, 60.0, 120.0, 250.0, 400.0 }) {
		synth.reset();
		EngineAudioInput in = driving();
		in.clutch_slip = slip;
		const std::vector<float> v = render(synth, in, 60);
		const std::vector<float> tail(v.begin() + v.size() / 2, v.end());
		const double r = rms(tail);
		row += "  " + std::to_string(static_cast<int>(slip)) + ":" + std::to_string(r);
		CHECK(r > previous);
		previous = r;
	}
	MESSAGE("clutch rms against slip rad/s:" << row);
}

TEST_CASE("shift direction comes off the gear the solver flips") {
	// A downshift is `CLACK_DOWNSHIFT_SCALE` = 0.70 of an upshift. The synth is
	// never told the direction; it latches the gear at the rising edge and compares
	// at the falling edge, because `drivetrain.h` publishes the OLD gear for the
	// whole shift. This is the test that would fail if that assumption were wrong.
	auto engage_peak = [](int from, int to) {
		ShiftSynth synth;
		synth.configure(shift_defaults(), RATE);
		EngineAudioInput in = driving(from);
		std::vector<float> scratch(512, 0.0f);
		synth.publish(in);
		synth.render(scratch.data(), 512); // settle, no edge
		in.shifting = true;
		synth.publish(in);
		synth.render(scratch.data(), 512); // disengage
		in.shifting = false;
		in.gear = to;
		synth.publish(in);
		std::vector<float> out(512, 0.0f);
		synth.render(out.data(), 512); // engage
		return peak(out);
	};

	const double up = engage_peak(3, 4);
	const double down = engage_peak(4, 3);
	MESSAGE("engagement peak: upshift " << up << ", downshift " << down
			<< ", ratio " << (down / up));
	CHECK(down < up);
	// Both clacks are the same PRNG draw from the same reseeded state, so the two
	// runs are identical but for the direction scale -- which makes this an exact
	// relative check rather than a statistical one.
	CHECK(close_rel(down / up, kart::core::shift_tuning::CLACK_DOWNSHIFT_SCALE, 0.001));
}

TEST_CASE("a shift made off the throttle is quieter and never silent") {
	// `CLACK_LOAD_WEIGHT` is 0.65, so at load 0 the clack keeps 35% of its level:
	// the dogs still move. At 0.0 the layer would be a constant-level click
	// whatever the driver is doing, which is the tell of a synthesized gearbox; at
	// 1.0 a shift off the throttle would be silent, which is wrong.
	auto engage_peak = [](double load) {
		ShiftSynth synth;
		synth.configure(shift_defaults(), RATE);
		EngineAudioInput in = driving();
		in.load = load;
		std::vector<float> scratch(512, 0.0f);
		synth.publish(in);
		synth.render(scratch.data(), 512);
		in.shifting = true;
		synth.publish(in);
		synth.render(scratch.data(), 512);
		in.shifting = false;
		in.gear = in.gear + 1;
		synth.publish(in);
		std::vector<float> out(512, 0.0f);
		synth.render(out.data(), 512);
		return peak(out);
	};

	const double full = engage_peak(1.0);
	const double none = engage_peak(0.0);
	const double w = kart::core::shift_tuning::CLACK_LOAD_WEIGHT;
	MESSAGE("engagement peak: load 1.0 " << full << ", load 0.0 " << none
			<< ", ratio " << (none / full) << ", predicted " << (1.0 - w));
	CHECK(none > 0.0);
	CHECK(close_rel(none / full, 1.0 - w, 0.001));
}

TEST_CASE("the curb rumble frequency is v over lambda") {
	// **The one sourced claim in either file.** `surface.h` carries
	// `ripple_wavelength = 0.15 m` for `SURFACE_CURB` as the definition M5's curb
	// mesh is generated from, so a wheel crossing it at `v` is excited at `v/lambda`
	// -- and the collider the wheel actually rides agrees with that number by
	// construction because there is one owner of it.
	//
	// Measured by counting the modulation's zero-crossings in the rendered
	// envelope, NOT by asking the class what rate it chose. A class asked about
	// itself agrees with itself.
	const double lambda = kart::core::surface(kart::core::SURFACE_CURB).ripple_wavelength;
	CHECK(lambda > 0.0);

	std::string row;
	for (double v : { 10.0, 20.0, 30.0 }) {
		RollSynth synth;
		RollAudioConfig config;
		config.curb_modulation_depth = 1.0; // full depth: the modulator touches zero
		synth.configure(config, RATE);

		EngineAudioInput in = driving();
		in.speed_ms = v;
		in.surface = kart::core::SURFACE_CURB;
		const std::vector<float> out = render_roll(synth, in, 200);

		// **The first version of this measurement counted mean-crossings of a
		// peak-follower envelope and it did not work**: it reported 60 / 65 / 52 Hz
		// where the law says 67 / 133 / 200, i.e. it saturated instead of tracking.
		// The follower's 256-sample window is 5.3 ms and the ripple period at
		// 20 m/s is 7.5 ms, so the window was averaging away the thing it was
		// measuring. That is worth recording rather than quietly replacing: an
		// envelope follower slower than the modulation it is following will always
		// report a plausible number that is mostly its own time constant.
		//
		// This demodulates instead. |x| low-passed well below the carrier and well
		// above the modulation gives the envelope; the modulation is DETERMINISTIC
		// (a rectified cosine of known phase advance), so its spectrum is a line
		// and an argmax over a candidate grid is a legitimate estimator here --
		// unlike CLAUDE.md's Rayleigh trap, which is about estimating a filter's
		// response from noise. The carrier is noise; the modulator is not.
		//
		// The search band is deliberately wide, 20-400 Hz, so that landing on
		// `v/lambda` is evidence rather than the grid's doing.
		std::vector<double> env;
		{
			// One-pole at 500 Hz: above the 200 Hz ceiling of the modulation and
			// below the rolling band's own energy.
			const double a = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 500.0 / RATE);
			double y = 0.0;
			for (size_t i = 0; i < out.size(); ++i) {
				y += (std::fabs(static_cast<double>(out[i])) - y) * a;
				env.push_back(y);
			}
		}
		// Skip the level ramp: the 35 ms smoothing one-pole is still settling.
		const size_t skip = env.size() / 3;
		double mean = 0.0;
		for (size_t i = skip; i < env.size(); ++i) {
			mean += env[i];
		}
		mean /= static_cast<double>(env.size() - skip);

		double best = 0.0;
		double best_power = -1.0;
		for (double f = 20.0; f <= 400.0; f += 0.25) {
			double re = 0.0;
			double im = 0.0;
			const double w = 2.0 * 3.14159265358979323846 * f / RATE;
			for (size_t i = skip; i < env.size(); ++i) {
				const double d = env[i] - mean;
				const double p = w * static_cast<double>(i - skip);
				re += d * std::cos(p);
				im += d * std::sin(p);
			}
			const double power = re * re + im * im;
			if (power > best_power) {
				best_power = power;
				best = f;
			}
		}
		const double measured = best;
		const double predicted = v / lambda;

		row += "\n      " + std::to_string(static_cast<int>(v)) + " m/s: measured " +
				std::to_string(measured) + " Hz, v/lambda " + std::to_string(predicted) + " Hz";
		// 3%: the modulator is deterministic and the grid is 0.25 Hz, so this is a
		// real precision claim and not a shape check. An octave error would be 100%
		// out and a wrong wavelength would be a clean scale factor.
		CHECK(close_rel(measured, predicted, 0.03));
	}
	MESSAGE("curb rumble, lambda = " << lambda << " m:" << row);

	// And every other surface has no periodic structure at all, which falls out of
	// `ripple_wavelength` being 0.0 rather than needing a branch on the enum.
	for (int s : { kart::core::SURFACE_ASPHALT, kart::core::SURFACE_GRASS, kart::core::SURFACE_DIRT }) {
		CHECK(RollSynth::rumble_hz_for(s, 25.0) == 0.0);
	}
}

TEST_CASE("the rolling layer realizes the dB per doubling written on it") {
	// The defect this guards shipped TWICE in `scrub_wind.h` -- once for scrub and
	// once for wind, a commit apart. A layer whose filter's own white-noise gain
	// moves with its cutoff, and whose cutoff moves with speed, realizes its speed
	// law plus whatever the filter is adding. The wind layer measured 20.49 dB per
	// doubling against a law that says 18.0, and `wind_gain` moved every time
	// `wind_cutoff_hz_per_ms` did.
	//
	// Measured across a doubling in the middle of the kart's range, where neither
	// the cutoff floor nor the Nyquist guard binds.
	RollAudioConfig config;
	const double want = config.db_per_speed_doubling;

	auto level_at = [&](double v) {
		RollSynth synth;
		synth.configure(config, RATE);
		EngineAudioInput in = driving();
		in.speed_ms = v;
		in.surface = kart::core::SURFACE_ASPHALT;
		const std::vector<float> out = render_roll(synth, in, 120);
		const std::vector<float> tail(out.begin() + out.size() / 2, out.end());
		return rms(tail);
	};

	std::string row;
	double worst = 0.0;
	for (double v : { 8.0, 12.0, 16.0 }) {
		const double lo = level_at(v);
		const double hi = level_at(v * 2.0);
		const double measured = 20.0 * std::log10(hi / lo);
		row += "\n      " + std::to_string(static_cast<int>(v)) + " -> " +
				std::to_string(static_cast<int>(v * 2)) + " m/s: " +
				std::to_string(measured) + " dB";
		const double err = std::fabs(measured - want);
		if (err > worst) {
			worst = err;
		}
	}
	MESSAGE("rolling dB per doubling, law says " << want << ":" << row
			<< "\n      worst deviation " << worst << " dB");
	// An absolute tolerance in dB, which is the right unit here -- and 0.6 dB is
	// tight enough to catch the 2.49 dB the un-normalized wind filter was adding.
	CHECK(worst < 0.6);
}

TEST_CASE("rolling is louder and brighter on a rough surface, and silent at rest") {
	RollSynth synth;
	RollAudioConfig config;

	// A kart at rest rolls silently. `pow(0, n)` is 0 and nothing re-adds a floor.
	synth.configure(config, RATE);
	EngineAudioInput rest = driving();
	rest.speed_ms = 0.0;
	const std::vector<float> still = render_roll(synth, rest, 40);
	CHECK(peak(still) == 0.0);
	MESSAGE("stationary: peak " << peak(still));

	// And louder on rougher ground, at the same speed.
	//
	// **The curb is deliberately excluded from the ordering, and finding that out
	// is why this test earned its place.** The first version asserted monotonicity
	// across all four rows by `roughness` and the curb came back QUIETER than
	// asphalt -- 0.00494 against 0.00657 -- despite being rougher. That is not a
	// defect: the curb is the one surface with a modulator, and a rectified cosine
	// at depth `d` has mean `1 - d/2`, so at the shipped depth of 0.75 the curb
	// layer's mean amplitude is 0.625 of what the same roughness would give on a
	// flat surface. The wheel is genuinely unloaded for part of every ripple and
	// that is the sound of a curb.
	//
	// So the ordering claim is made over the three flat surfaces, and the curb gets
	// its own check below against the number the modulator's own shape predicts.
	// Asserting the four in one line would have been a test that had to be
	// weakened until it passed.
	std::string row;
	double previous = -1.0;
	for (int s : { kart::core::SURFACE_ASPHALT, kart::core::SURFACE_GRASS, kart::core::SURFACE_DIRT }) {
		synth.reset();
		EngineAudioInput in = driving();
		in.speed_ms = 20.0;
		in.surface = s;
		const std::vector<float> out = render_roll(synth, in, 80);
		const std::vector<float> tail(out.begin() + out.size() / 2, out.end());
		const double r = rms(tail);
		row += "\n      " + std::string(kart::core::surface(s).name) + " (roughness " +
				std::to_string(kart::core::surface(s).roughness) + "): rms " + std::to_string(r);
		// Ordered by roughness: asphalt 0.25, grass 0.80, dirt 0.90.
		CHECK(r > previous);
		previous = r;
	}
	MESSAGE("rolling rms by surface at 20 m/s, flat surfaces only:" << row);
}

TEST_CASE("the curb's modulation costs exactly what its own shape says") {
	// The curb is quieter in mean than its roughness alone would give, because the
	// modulator is a rectified cosine `0.5*(1-cos)` whose mean is 0.5, so an
	// amplitude of `(1-d) + d*bump` has mean `1 - d/2`. At the shipped depth 0.75
	// that is 0.625.
	//
	// Measured by rendering the curb twice at the same speed and roughness-driven
	// settings, once with the modulation at its shipped depth and once at zero. The
	// ratio of the two RMS values is a prediction with no free parameter in it.
	auto curb_rms = [](double depth) {
		RollSynth synth;
		RollAudioConfig c;
		c.curb_modulation_depth = depth;
		synth.configure(c, RATE);
		EngineAudioInput in = driving();
		in.speed_ms = 20.0;
		in.surface = kart::core::SURFACE_CURB;
		const std::vector<float> out = render_roll(synth, in, 200);
		const std::vector<float> tail(out.begin() + out.size() / 2, out.end());
		return rms(tail);
	};

	const double d = RollAudioConfig().curb_modulation_depth;
	const double flat = curb_rms(0.0);
	const double rippled = curb_rms(d);
	const double measured = rippled / flat;

	// RMS rather than mean amplitude, so the prediction is the RMS of the
	// modulator: mean of `((1-d) + d*bump)^2` over a cycle, where bump is
	// `0.5*(1-cos)`. E[bump] = 1/2 and E[bump^2] = 3/8, so
	// E[a^2] = (1-d)^2 + 2(1-d)d(1/2) + d^2(3/8), and the carrier is independent of
	// the modulator so the two multiply.
	const double predicted = std::sqrt((1.0 - d) * (1.0 - d) + (1.0 - d) * d + d * d * 0.375);
	MESSAGE("curb modulation depth " << d << ": rms ratio measured " << measured
			<< ", predicted " << predicted);
	CHECK(close_rel(measured, predicted, 0.03));
	// And it is a reduction, which is the physical claim: a wheel unloaded for part
	// of every ripple radiates less, not more.
	CHECK(measured < 1.0);
}

TEST_CASE("no operating point writes past full scale") {
	// The scrub layer wrote a peak of 1.2546 at its shipped gain for two
	// milestones. Both layers here share `soft_clip` with the engine note and with
	// scrub and wind, so there is one answer to what full scale is -- and this
	// checks that the shared ceiling is actually in the path rather than merely
	// included.
	double worst = 0.0;
	std::string row;

	for (double gain : { 0.055, 0.3, 1.0 }) {
		ShiftSynth synth;
		ShiftAudioConfig c = shift_defaults();
		c.clack_gain = gain;
		c.clutch_gain = gain;
		synth.configure(c, RATE);
		EngineAudioInput in = driving();
		in.clutch_slip = 500.0;
		in.shifting = true;
		std::vector<float> scratch(512, 0.0f);
		synth.publish(in);
		synth.render(scratch.data(), 512);
		double p = peak(scratch);
		in.shifting = false;
		in.gear += 1;
		synth.publish(in);
		synth.render(scratch.data(), 512);
		const double p2 = peak(scratch);
		p = p > p2 ? p : p2;
		row += "\n      shift gain " + std::to_string(gain) + ": peak " + std::to_string(p);
		if (p > worst) {
			worst = p;
		}
	}
	for (double gain : { 0.022, 0.3, 1.0 }) {
		RollSynth synth;
		RollAudioConfig c;
		c.roll_gain = gain;
		synth.configure(c, RATE);
		EngineAudioInput in = driving();
		in.speed_ms = 40.0;
		in.surface = kart::core::SURFACE_DIRT;
		const std::vector<float> out = render_roll(synth, in, 60);
		const double p = peak(out);
		row += "\n      roll gain " + std::to_string(gain) + ": peak " + std::to_string(p);
		if (p > worst) {
			worst = p;
		}
	}
	MESSAGE("worst peak across every operating point: " << worst << row);
	CHECK(worst <= 1.0);
}

TEST_CASE("identical input renders bit-identical output") {
	// The same determinism property the rest of src/core/ is held to
	// (ARCHITECTURE.md §8). Both synths carry their own explicitly seeded PCG32
	// stream for exactly this.
	auto run_shift = []() {
		ShiftSynth synth;
		synth.configure(shift_defaults(), RATE);
		EngineAudioInput in = driving();
		in.clutch_slip = 90.0;
		std::vector<float> out = render(synth, in, 4);
		in.shifting = true;
		const std::vector<float> a = render(synth, in, 1);
		out.insert(out.end(), a.begin(), a.end());
		return out;
	};
	const std::vector<float> a = run_shift();
	const std::vector<float> b = run_shift();
	REQUIRE(a.size() == b.size());
	size_t differing = 0;
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i] != b[i]) {
			++differing;
		}
	}
	MESSAGE("shift layer, " << a.size() << " samples, differing " << differing);
	CHECK(differing == 0);

	auto run_roll = []() {
		RollSynth synth;
		synth.configure(RollAudioConfig(), RATE);
		EngineAudioInput in = driving();
		in.speed_ms = 22.0;
		in.surface = kart::core::SURFACE_CURB;
		return render_roll(synth, in, 6);
	};
	const std::vector<float> c = run_roll();
	const std::vector<float> d = run_roll();
	REQUIRE(c.size() == d.size());
	differing = 0;
	for (size_t i = 0; i < c.size(); ++i) {
		if (c[i] != d[i]) {
			++differing;
		}
	}
	MESSAGE("rolling layer, " << c.size() << " samples, differing " << differing);
	CHECK(differing == 0);
}

TEST_CASE("the block size does not change what is rendered") {
	// `audio_state.h`'s rate-mismatch rule: a mix block is whatever the device
	// chooses, so a layer whose output depended on the block size would sound
	// different on a different audio device. The clack's edge detection is
	// per-block, which is exactly the code most able to get this wrong -- a shift
	// spans about six blocks at 512 frames and one at 4096.
	auto run = [](int frames, int blocks) {
		RollSynth synth;
		synth.configure(RollAudioConfig(), RATE);
		EngineAudioInput in = driving();
		in.speed_ms = 22.0;
		return render_roll(synth, in, blocks, frames);
	};
	const std::vector<float> small = run(128, 32);
	const std::vector<float> large = run(1024, 4);
	REQUIRE(small.size() == large.size());
	double worst = 0.0;
	for (size_t i = 0; i < small.size(); ++i) {
		const double d = std::fabs(static_cast<double>(small[i]) - static_cast<double>(large[i]));
		if (d > worst) {
			worst = d;
		}
	}
	MESSAGE("128x32 against 1024x4, worst sample difference " << worst);
	// Not bit-identical: the level one-pole is advanced per sample but its TARGET
	// is recomputed per block, and with constant input the target is constant, so
	// the two agree exactly. If this ever becomes non-zero the target has grown a
	// per-block dependence that a device with a different buffer would hear.
	CHECK(worst == 0.0);
}

TEST_CASE("the band chain normalization is the theoretical one") {
	// `ShiftSynth::band_chain_rms` claims `sqrt(pi * Q * f0 / (2 fs))`, which is the
	// white-noise RMS through an ideal two-pole band-pass. Unlike the scrub chain
	// there is no tilt low-pass after it, so the theoretical exponent should hold
	// with no fitted correction -- `scrub_wind.h`'s `SCRUB_CHAIN_RMS_Q_EXPONENT` is
	// 0.61 rather than 0.5 precisely because its tilt sits at the band center.
	//
	// Measured by pushing white noise through the same `Svf` the synth uses.
	// A residual that climbs monotonically with Q is a wrong exponent; a flat
	// offset is a wrong constant. Both are printed so the next reader can tell.
	auto measure = [](double f0, double q) {
		kart::core::Svf filter;
		filter.set(f0, q, RATE);
		kart::core::Pcg32 rng(0, 0x1234567890ABCDEFULL);
		const int n = 400000;
		const int skip = n / 10;
		double sum = 0.0;
		for (int i = 0; i < n; ++i) {
			double lp = 0.0;
			double bp = 0.0;
			filter.process(rng.next_double() * 2.0 - 1.0, lp, bp);
			// The source is uniform on [-1,1), RMS 1/sqrt(3). Divided back out so
			// this measures the FILTER's gain against unit-RMS noise, which is the
			// convention `SCRUB_CHAIN_RMS_C`'s own table uses.
			if (i > skip) {
				sum += bp * bp;
			}
		}
		return std::sqrt(sum / (n - skip - 1)) * std::sqrt(3.0);
	};

	std::string row;
	double worst = 0.0;

	// Q sweep at the clack's own center. A residual that climbs monotonically here
	// is a wrong exponent; a flat offset is a wrong constant. That is exactly how
	// the closed form this replaced was diagnosed.
	row += "\n      Q sweep at f0 2200 Hz:";
	for (double q : { 0.5, 0.8, 1.2, 3.0, 8.0 }) {
		const double m = measure(2200.0, q);
		const double p = ShiftSynth::band_chain_rms(2200.0, q, RATE);
		const double ratio = m / p;
		row += "\n        Q " + std::to_string(q) + ": measured " + std::to_string(m) +
				", fit " + std::to_string(p) + ", ratio " + std::to_string(ratio);
		const double err = std::fabs(ratio - 1.0);
		if (err > worst) {
			worst = err;
		}
	}

	// **And an f0 sweep, which the first version of this test did not have.** The
	// fit was taken at one center frequency, so `sqrt(f0/fs)` was assumed rather
	// than checked -- and a test that only sweeps the parameter you fitted cannot
	// tell a correct law from a coincidence at one point.
	row += "\n      f0 sweep at Q 1.2:";
	for (double f0 : { 300.0, 900.0, 2200.0, 6000.0 }) {
		const double m = measure(f0, 1.2);
		const double p = ShiftSynth::band_chain_rms(f0, 1.2, RATE);
		const double ratio = m / p;
		row += "\n        f0 " + std::to_string(static_cast<int>(f0)) + " Hz: measured " +
				std::to_string(m) + ", fit " + std::to_string(p) + ", ratio " + std::to_string(ratio);
		const double err = std::fabs(ratio - 1.0);
		if (err > worst) {
			worst = err;
		}
	}

	MESSAGE("band chain RMS against the fit:" << row << "\n      worst error " << worst);
	CHECK(worst < 0.08);
}
