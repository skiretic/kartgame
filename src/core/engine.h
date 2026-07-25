#ifndef KART_CORE_ENGINE_H
#define KART_CORE_ENGINE_H

#include "core/units.h"

#include <cmath>
#include <cstddef>

// The KZ engine. ARCHITECTURE.md §6.3: "Torque curve lookup against rpm, scaled
// by throttle", plus the two things §6.3 says are easy to leave out and badly
// missed when they are — heavy engine braking, and an over-rev on a downshift
// that is possible and punished.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## What this models
//
// A 125 cc water-cooled reed-valve two-stroke to CIK-FIA KZ rules: 54 mm bore,
// 54.4 mm stroke, a 30 mm carburetor, and a tuned exhaust that does most of the
// work. Everything a driver feels about such an engine comes out of one fact:
// the expansion chamber only resonates over a narrow band of engine speeds, and
// outside that band the cylinder does not scavenge properly. That is why the
// curve below climbs so steeply through 9,000-11,000 rpm and why it falls off a
// cliff after 14,000 — not because the numbers were drawn for drama, but because
// a pipe is a tuned length and it is only right at one frequency.
//
// ## Where the numbers come from
//
// docs/REFERENCES.md has the sources; the short version is that the *shape* is
// constrained by published operating ranges and the *scale* is anchored on
// kz_reference.h's 45 hp at 13,000 rpm, which is the CIK-configuration figure
// rather than the 50-53 hp a dyno operator will quote for a factory engine.
// Vroomkart's dyno comparison of a TM KZ1R states the engine is worked between
// 11,500 and 14,500 rpm in the high gears, extendable to 15,000; a KartPulse
// thread has a TM KZ10 "limited to 14,000 rpm"; another has an engine builder
// putting the usable band at about 2,500 rpm wide, against 9,000-12,000 for a
// single-speed kart. The curve here reproduces all three: peak torque at 12,000,
// 90% of it held from 10,740 to 13,400 rpm — a 2,660 rpm band — and 47% of peak
// torque left at 8,000 rpm, which is the "dead below 9,000" of issue #36.
//
// What is *not* sourced is a dyno trace with numbers on the axes. Nobody
// publishes one: Vroomkart prints its KZ curves deliberately without a power
// scale, and every builder's figure is relative to his own dyno. So the
// intermediate points of `WOT_CURVE` are interpolated between hard constraints
// rather than measured, and that is stated here so a later session does not
// mistake them for data.
//
// ## Engine braking is a first-class term
//
// A closed-throttle two-stroke pumps against a shut 30 mm slide, and the KZ's
// primary reduction of 4.17 and first gear of 2.54 mean the crank sees the road
// through a 14.7:1 multiplication. Eight newton-meters of crankshaft drag —
// which is a third of peak torque and nothing at all in absolute terms — becomes
// 600 N at the contact patch, and 600 N on 175 kg is a third of a g. That is the
// whole of why §6.3 says lifting the throttle can shape corner entry more than
// the brakes do, and it is an arithmetic result, not an effect that has to be
// exaggerated to be felt.

namespace kart::core {

// One point on the wide-open-throttle curve: engine speed in rpm, brake torque
// at the crankshaft in newton-meters.
//
// Brake torque, not indicated torque — a dyno figure is already net of the
// engine's own friction, which is why `drag_torque` below is added back before
// the throttle scales it and subtracted again afterwards.
struct TorquePoint {
	double rpm;
	double torque;
};

// The curve. Anchored at three places and interpolated between them: it must
// make kz_reference.h's peak power at kz_reference.h's peak-power rpm, it must
// hold 80% of peak torque only inside kz_reference.h's powerband, and it must be
// weak enough below 9,000 rpm that a driver who lets it drop there has made a
// mistake. Everything else follows from a two-stroke's shape — a slow climb off
// the bottom, a knee where the pipe comes in around 9,000, a short flat top, and
// a fast fall once the exhaust goes out of phase.
inline constexpr TorquePoint WOT_CURVE[] = {
	{ 2000.0, 5.0 }, // Just above idle. The engine will pull a kart at walking
	{ 4000.0, 7.0 }, // pace here and nothing more; a KZ is push-started and
	{ 6000.0, 9.0 }, // launched on a slipping clutch precisely because of this.
	{ 7000.0, 10.5 },
	{ 8000.0, 12.4 }, // 47% of peak. This is "dead".
	{ 9000.0, 16.0 }, // The pipe starts to work. Note the slope from here.
	{ 10000.0, 21.0 },
	{ 11000.0, 24.5 },
	{ 11500.0, 25.6 },
	{ 12000.0, 26.2 }, // Peak torque.
	{ 12500.0, 25.6 },
	{ 13000.0, 24.7 }, // Peak *power*: 24.7 N·m at 1361 rad/s is 33.6 kW = 45 hp.
	{ 13500.0, 23.3 },
	{ 14000.0, 20.5 }, // Falling fast. Past here the pipe is out of phase and
	{ 14500.0, 17.0 }, // the engine is turning itself, not the kart.
	{ 15000.0, 13.0 },
	{ 16000.0, 6.0 }, // Only reachable by over-revving it on a downshift.
};

inline constexpr std::size_t WOT_CURVE_POINTS = sizeof(WOT_CURVE) / sizeof(WOT_CURVE[0]);

// Watts per mechanical horsepower, so that a figure quoted in hp — which every
// karting source quotes — can be checked against a torque curve in SI without
// the conversion being retyped at each call site.
inline constexpr double WATTS_PER_HP = 745.699872;

class Engine {
public:
	// Rotational inertia of everything turning at crankshaft speed, kg·m².
	//
	// **Estimated, not sourced.** A KZ crankshaft is a pair of full-circle steel
	// webs on a 27.2 mm throw; treating each as a 110 mm disc 22 mm thick gives
	// 1.6 kg and 2.5e-3 kg·m² apiece, and the rod, piston and ignition rotor add
	// a little. No manufacturer publishes the figure and no dealer listing gives
	// a crank mass, so 5.5e-3 is a geometric estimate and is flagged as one in
	// docs/REFERENCES.md.
	//
	// It matters more than its size suggests, because the drivetrain reflects it
	// to the axle through the *square* of the total ratio: 1.19 kg·m² in first,
	// which is 63 kg of apparent mass at a 0.1375 m tire. A KZ in first gear is
	// meaningfully heavier than a KZ in sixth, and this is the term that says so.
	double inertia = 0.0055;

	// Idle. A KZ has no starter and no throttle stop worth the name; the idle
	// governor here is a modeling convenience that holds the engine alive when
	// nothing is asking anything of it, and it is deliberately weak — 6 N·m is
	// not enough to move the kart, so idling in gear against a closed clutch is
	// fine and idling in gear against a *closed* clutch lever is a stall.
	double idle_rpm = 2000.0;
	double idle_band_rpm = 400.0;
	double idle_torque_nm = 6.0;

	// Below this the engine has stopped. Nothing here enforces it — the
	// drivetrain owns the stall, because only it knows whether the clutch is
	// still dragging the crank down.
	double stall_rpm = 1000.0;

	// The soft cut. From `soft_cut_rpm` the ignition starts dropping sparks and
	// drive torque tapers linearly to nothing at `hard_cut_rpm`; a driver feels
	// it as the engine going soft and then flat rather than hitting a wall.
	// 14,300-14,800 brackets the 14,000 rpm limit quoted for a TM KZ10 and the
	// 14,500 rpm working ceiling Vroomkart quotes for a KZ1R.
	double soft_cut_rpm = 14300.0;
	double hard_cut_rpm = 14800.0;

	// Closed-throttle drag: `drag_base_nm + drag_per_rads * omega`, newton-meters
	// resisting rotation. A two-stroke's closed-throttle loss is dominated by
	// pumping against the shut slide, which rises with speed, plus a roughly
	// constant friction term — hence the affine form rather than a table.
	//
	// **The slope is chosen, not measured**, and it was chosen against an
	// outcome rather than a component: 0.00462 N·m·s puts closed-throttle drag at
	// 7.9 N·m at 12,000 rpm, which is 0.29 g of deceleration in second gear at
	// 60 km/h once the reflected inertia is counted. §6.3 asks for a
	// deceleration strong enough to shape corner entry against brakes worth
	// 1.5-2.0 g, and a fifth of the braking effort with no pedal input is that.
	double drag_base_nm = 2.0;
	double drag_per_rads = 0.00462;

	// The published curve, N·m, by linear interpolation — what a healthy engine
	// of this type makes on a dyno. Linear and not a spline on purpose: a spline
	// through hand-placed points overshoots between them, and an engine curve
	// with a bump in it that nobody put there is a tuning bug that takes an
	// afternoon to find.
	double catalog_torque(double rpm) const {
		if (rpm <= 0.0) {
			return 0.0;
		}
		if (rpm <= WOT_CURVE[0].rpm) {
			// Below the first point, fair the curve into the origin. A stopped
			// engine makes no torque, and a cranking one makes very little.
			return WOT_CURVE[0].torque * (rpm / WOT_CURVE[0].rpm);
		}
		for (std::size_t index = 0; index + 1 < WOT_CURVE_POINTS; ++index) {
			const TorquePoint &low = WOT_CURVE[index];
			const TorquePoint &high = WOT_CURVE[index + 1];
			if (rpm <= high.rpm) {
				const double span = high.rpm - low.rpm;
				const double fraction = (rpm - low.rpm) / span;
				return low.torque + fraction * (high.torque - low.torque);
			}
		}
		// Past the last point. Kept at zero rather than extrapolated: the curve
		// already covers 16,000 rpm, an engine there is being destroyed, and a
		// negative extrapolated torque would look like engine braking instead.
		return 0.0;
	}

	// What *this* engine makes at wide-open throttle right now, N·m: the
	// published curve less whatever an over-rev has cost it. Everything derived —
	// power, the peak, the powerband — goes through here rather than through
	// `catalog_torque`, so that a damaged engine is damaged in the telemetry and
	// in the tuning UI and not only in the torque the axle sees.
	double wide_open_torque(double rpm) const {
		return catalog_torque(rpm) * health();
	}

	// Torque resisting rotation with the throttle shut, N·m, positive.
	double drag_torque(double rpm) const {
		if (rpm <= 0.0) {
			return 0.0;
		}
		return drag_base_nm + drag_per_rads * rpm_to_rads(rpm);
	}

	// The idle governor's contribution, N·m. Proportional inside a band below
	// idle and nothing above it, so it cannot add anything to a curve a test is
	// measuring.
	double idle_assist(double rpm) const {
		if (rpm >= idle_rpm) {
			return 0.0;
		}
		const double below = (idle_rpm - rpm) / idle_band_rpm;
		const double fraction = below < 1.0 ? below : 1.0;
		return idle_torque_nm * fraction;
	}

	// How much drive torque the ignition is still letting through, 0 to 1.
	//
	// The limiter can only remove *drive*. It cannot stop the crankshaft being
	// turned by the road through a locked gearbox, which is exactly why an
	// over-rev on a downshift is possible at all — see `over_rev` below. A rev
	// limiter is not a speed limiter, and modeling it as one would delete the
	// mistake issue #36 asks to keep.
	double limiter_scale(double rpm) const {
		if (rpm <= soft_cut_rpm) {
			return 1.0;
		}
		if (rpm >= hard_cut_rpm) {
			return 0.0;
		}
		return (hard_cut_rpm - rpm) / (hard_cut_rpm - soft_cut_rpm);
	}

	// Crankshaft torque, N·m, signed. Negative is the engine slowing whatever it
	// is attached to.
	//
	// The blend deserves a sentence, because the obvious version is wrong. A
	// dyno curve is *brake* torque — already net of the engine's own losses — so
	// scaling it by throttle and adding a separate drag term would subtract the
	// friction twice at part throttle. Instead the drag is added back to recover
	// the gross torque the combustion produces, the throttle scales that, and the
	// drag comes off once at the end. At full throttle the result is exactly the
	// published curve; at zero throttle it is exactly minus the drag; in between
	// it crosses zero where the engine is doing just enough to turn itself, which
	// is where a driver feels the on-off transition.
	double torque(double rpm, double throttle) const {
		const double drag = drag_torque(rpm);
		const double gross = wide_open_torque(rpm) + drag;
		const double demanded = throttle < 0.0 ? 0.0 : (throttle > 1.0 ? 1.0 : throttle);
		const double driven = (gross * demanded + idle_assist(rpm)) * limiter_scale(rpm);
		return driven - drag;
	}

	// Power at wide-open throttle, watts. `P = T * omega`, computed rather than
	// tabulated, so that a change to the torque curve cannot leave a stale power
	// figure behind it.
	double power(double rpm) const {
		return wide_open_torque(rpm) * rpm_to_rads(rpm);
	}

	// Is the engine turning faster than the ignition can defend against? Reachable
	// only mechanically — by dropping a gear at a speed the next ratio cannot
	// support, or by a locked driveline on a downhill.
	bool over_rev(double rpm) const {
		return rpm > hard_cut_rpm;
	}

	// Accumulate the cost of an over-rev. Called once per substep by the
	// drivetrain.
	//
	// The consequence is deliberately not a fault light: peak torque falls by the
	// accumulated damage, permanently, until something resets it. A driver who
	// over-revs by 2,000 rpm for a second loses a fifth of the engine, which is
	// the right order of magnitude for an event that in reality bends a rod or
	// picks up a piston. It is scaled by *how far* past the limit and *how long*,
	// because a momentary flick past the cut on a downshift should cost nearly
	// nothing and holding it there should end the session.
	void accumulate_over_rev(double rpm, double dt) {
		if (!over_rev(rpm) || dt <= 0.0) {
			return;
		}
		const double excess = rpm - hard_cut_rpm;
		damage_ += (excess / 10000.0) * dt;
		if (damage_ > 1.0) {
			damage_ = 1.0;
		}
	}

	// 0 = fresh, 1 = destroyed.
	double damage() const { return damage_; }
	void repair() { damage_ = 0.0; }

	// The multiplier damage puts on the torque curve.
	double health() const { return 1.0 - damage_; }

	// Where the curve peaks, by a fine scan. There is no closed form for a
	// piecewise-linear curve times omega and there does not need to be: this runs
	// once in a test and once in a tuning UI, never in the solver.
	double peak_power_rpm(double step = 5.0) const {
		double best_rpm = 0.0;
		double best_power = -1.0;
		for (double rpm = 0.0; rpm <= 16000.0; rpm += step) {
			const double value = power(rpm);
			if (value > best_power) {
				best_power = value;
				best_rpm = rpm;
			}
		}
		return best_rpm;
	}

	double peak_torque_rpm(double step = 5.0) const {
		double best_rpm = 0.0;
		double best_torque = -1.0;
		for (double rpm = 0.0; rpm <= 16000.0; rpm += step) {
			const double value = wide_open_torque(rpm);
			if (value > best_torque) {
				best_torque = value;
				best_rpm = rpm;
			}
		}
		return best_rpm;
	}

	double peak_torque(double step = 5.0) const {
		return wide_open_torque(peak_torque_rpm(step));
	}

	// The rpm range over which the engine makes at least `fraction` of its peak
	// torque. This is the number that decides how a KZ is driven — a band this
	// narrow is why the gearbox is a real subsystem and not a parameter — so it
	// is exposed rather than left for each caller to rediscover by scanning.
	// Returns false if the fraction is never reached.
	bool powerband(double fraction, double &low_rpm, double &high_rpm, double step = 5.0) const {
		const double threshold = peak_torque(step) * fraction;
		bool found = false;
		for (double rpm = 0.0; rpm <= 16000.0; rpm += step) {
			if (wide_open_torque(rpm) >= threshold) {
				if (!found) {
					low_rpm = rpm;
					found = true;
				}
				high_rpm = rpm;
			}
		}
		return found;
	}

private:
	double damage_ = 0.0;
};

} // namespace kart::core

#endif // KART_CORE_ENGINE_H
