#ifndef KART_CORE_SURFACE_H
#define KART_CORE_SURFACE_H

#include <cmath>

// The surface table. ARCHITECTURE.md §6's Components row: "asphalt / curb /
// grass / dirt -> grip multiplier, roughness, particle and audio hooks", and
// issue #42.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## What a surface multiplier is here, and what it is not
//
// `Surface::grip` scales `TireSlip::surface_grip`, which multiplies **both**
// tire axes in `Tire::evaluate` — lateral capacity, longitudinal capacity, and
// the friction ellipse they are scaled onto. On a vehicle with no downforce the
// lateral acceleration a tire can hold *is* its friction coefficient, so the
// number in this table is very close to a direct statement of "what fraction of
// the kart's 2.1 g is left here". tests/core/test_surface.cpp measures exactly
// that against tire.h rather than restating it.
//
// The multipliers are **not** ratios lifted from a table of road-car friction
// coefficients, and getting that wrong would be the easy mistake. The published
// coefficients are all measured with ordinary road tires, whose own dry-asphalt
// coefficient is 0.65-0.70. A kart slick's is 2.10 (tire.h). Scaling grass by
// "grass over asphalt for a road car" would hand a slick 1.1 g on a lawn.
//
// The split this table uses instead, stated once because everything below
// depends on it:
//
//   * On **hard** surfaces — asphalt, painted concrete — the shear plane is
//     rubber against stone, so the compound is what changes between a road tire
//     and a slick and a *ratio* between two hard surfaces transfers. The curb
//     number is a ratio.
//   * On **deformable** surfaces — grass, compacted earth — the shear plane is
//     inside the terrain. Soil fails at the soil's shear strength no matter what
//     is standing on it, so the published coefficient transfers as an
//     **absolute** number and the multiplier is that coefficient divided by the
//     tire's own 2.10. Grass and dirt are absolutes.
//
// Every number, its source and what could not be sourced are in
// docs/REFERENCES.md's surfaces section.
//
// ## What makes a curb a curb
//
// Not the grip number. A curb is *intermittent*: a kart riding one is repeatedly
// unloaded and reloaded at a few tens of hertz, and the load modulation is what
// costs grip and what upsets the chassis. That is geometry, and this file's
// position is that geometry belongs in the mesh: at M5 `track.json` builds the
// curb with its ripple modeled, and the per-wheel raycast finds it for free.
//
// So `ripple_amplitude` and `ripple_wavelength` here are the **definition the
// M5 curb mesh is built from**, and `ripple_height` is that definition as code
// so there is one owner for the shape. The solver must not add this displacement
// on top of a raycast that already found it — that would double-count, and
// double-counting a bump is how a kart ends up launched off a curb it should
// have ridden. Issue #42's "curbs are distinct from asphalt and grass" is
// carried at M3b by `grip` and the audio and particle hooks; the ripple lands
// with real geometry at M5.

namespace kart::core {

// The surface identifier, stored in `GroundQuery::surface`.
//
// **These integers are a wire format.** They end up in `data/track.json` at M5
// (ARCHITECTURE.md §11) and in any replay that records what a wheel was on, so
// they are numbered explicitly and a value never changes meaning. A new surface
// takes the next free number; nothing is ever renumbered or removed. Zero is
// asphalt so that a default-constructed `GroundQuery` describes the track rather
// than describing nothing.
enum SurfaceType : int {
	SURFACE_ASPHALT = 0,
	SURFACE_CURB = 1,
	SURFACE_GRASS = 2,
	SURFACE_DIRT = 3,

	SURFACE_COUNT = 4
};

// One row of the §6 surface table.
struct Surface {
	// Stable identifier for track data, save files and logs. Not a display
	// string — it is matched against, so it is lowercase ASCII and never
	// translated.
	const char *name;

	// The wire value. Duplicated into the row so that a lookup can be checked
	// against the index it was found at, which is how a reordered table gets
	// caught by a test rather than by a replay that plays back wrong.
	int type;

	// Multiplies `TireSlip::surface_grip`. Asphalt is 1.0 by definition: the
	// tire model is already a kart slick on a hot racing surface, so this column
	// says how much of that is left elsewhere.
	double grip;

	// How coarse this surface reads, 0 (glass) to 1 (loose). **Not a length.**
	// It is the mix parameter §12's tire-scrub and rolling audio and M10's
	// particle emitters take, and it is deliberately dimensionless so that
	// nobody mistakes it for a measured texture depth. The lengths that are
	// lengths are the two ripple fields below.
	double roughness;

	// The curb ripple, meters. Zero on every surface that has no periodic
	// structure. See the header comment: this is the definition M5's mesh is
	// generated from, not something the solver adds.
	double ripple_amplitude;
	double ripple_wavelength;

	// True when the shear plane is inside the terrain rather than at the rubber.
	// Two things read it: the grip column's derivation above, and M10's particle
	// hook, because a deformable surface is the one that throws material.
	bool deformable;

	// §12 and M10's hooks, resolved by name so that audio and particles can be
	// hung on a surface without the solver learning that either exists — issue
	// #42's third acceptance item. Empty on asphalt, which is the silent default
	// case rather than a bank of its own.
	const char *scrub_audio;
	const char *particle_effect;
};

// The table.
//
// Indexed by `SurfaceType`, and `surface()` below is the only sanctioned way to
// read it. The numbers:
//
//   asphalt 1.00  by definition — tire.h's 2.10 already is a slick on hot
//                 asphalt, so this row is the anchor and not a measurement.
//
//   curb    0.72  Burghardt et al. 2023 measured a pendulum test value of 49 on
//                 an asphalt surface and 35 on the same surface painted without
//                 anti-skid additives: 35/49 = 0.714. CIK-FIA Circuit
//                 Regulations §14.6 requires kart kerbs to be painted in two
//                 alternating colors, so a plain paint film is what a tire
//                 actually touches. The pendulum is conventionally run wet, so
//                 0.72 is likely conservative for a dry track — it is the first
//                 number a tuner should reach for, and it is low rather than
//                 high on purpose because a curb that gives away nothing is not
//                 a curb.
//
//   grass   0.18  Cenek et al. locked-wheel braking on dry rye-grass measured
//                 0.36 (long) and 0.38 (short); 0.37 / 2.10 = 0.176. The same
//                 paper's multi-surface skid case quotes dry chipseal at 0.70,
//                 so the road-car ratio would have been 0.53 — the value this
//                 table deliberately does not use, for the reason in the header.
//
//   dirt    0.17  CIK-FIA Circuit Regulations §7.5 requires the verge beside a
//                 kart track to be grass-covered or **compacted** ground, so the
//                 dirt this game runs onto is a compacted surface, not a loose
//                 one. Noon (1994), via Cenek et al.'s Table 1, gives 0.35 for a
//                 gravel and dirt road; 0.35 / 2.10 = 0.167.
//
// That grass and dirt come out within 6% of each other is not a mistake in the
// table, it is what the literature says: Cenek et al. write "the coefficient of
// dry rye-grass is comparable to gravel" in as many words. They are told apart
// by sound, by what they throw, and by how they look — not by grip. If they ever
// need to be told apart by feel, the honest lever is rolling resistance, which
// is not in this table because no sourced number was found for it.
inline constexpr Surface SURFACES[SURFACE_COUNT] = {
	{ "asphalt", SURFACE_ASPHALT, 1.00, 0.25, 0.000, 0.00, false, "", "" },
	{ "curb", SURFACE_CURB, 0.72, 0.35, 0.012, 0.15, false, "scrub_concrete", "curb_dust" },
	{ "grass", SURFACE_GRASS, 0.18, 0.80, 0.000, 0.00, true, "scrub_grass", "grass_clippings" },
	{ "dirt", SURFACE_DIRT, 0.17, 0.90, 0.000, 0.00, true, "scrub_dirt", "dirt_plume" },
};

// The row for a surface value, with asphalt as the fallback.
//
// Falls back rather than asserting because the caller is a track file and a
// replay, and both can carry a value this build has never heard of — a track
// authored against a later build, or a save from one. Failing closed onto the
// track surface keeps a kart driving; failing open onto whatever integer arrived
// indexes off the end of the table.
inline constexpr const Surface &surface(int type) {
	return (type >= 0 && type < SURFACE_COUNT) ? SURFACES[type] : SURFACES[SURFACE_ASPHALT];
}

// The multiplier to put in `TireSlip::surface_grip` and `GroundQuery`.
inline constexpr double grip_for(int type) {
	return surface(type).grip;
}

// The curb ripple as a height above the base plane, meters, against distance
// traveled along the curb.
//
// A rectified cosine rather than a sine: a ripple is a row of bumps standing on
// the curb's top face, so the profile has to be zero at its minimum and never
// negative. A sine would cut a groove into the concrete for half of every cycle.
//
// The two dimensions are **assumed, not sourced** — the CIK-FIA circuit
// regulations specify a kerb's paint and nothing about its profile, and no
// dimensioned drawing of a kart kerb was found. What they are anchored to is the
// tire: a rear slick of 0.1475 m radius deflecting about 2 mm has a contact
// chord of 2*sqrt(2*R*d) = 49 mm, and a tire bridges any ripple much shorter
// than its chord instead of following it. A 0.15 m wavelength is three chords,
// which is the shortest ripple a kart can still feel individually, and 12 mm is
// an amplitude a kart can ride without grounding its floor tray. Both are
// candidates for replacement by a photographed kerb — see the reference rule,
// ARCHITECTURE.md §5 item 10.
inline double ripple_height(const Surface &row, double distance) {
	if (row.ripple_amplitude <= 0.0 || row.ripple_wavelength <= 0.0) {
		return 0.0;
	}
	const double phase = 2.0 * 3.14159265358979323846 * distance / row.ripple_wavelength;
	return row.ripple_amplitude * 0.5 * (1.0 - std::cos(phase));
}

// A surface change under one wheel.
//
// The audio and particle hooks in issue #42's third acceptance item are edges,
// not levels: a gravel-hiss loop starts when a wheel arrives on dirt and a dust
// burst fires once. Polling `GroundQuery::surface` every tick and comparing it
// against last tick's is four lines that every consumer would otherwise write
// for itself, and would write differently.
struct SurfaceChange {
	bool changed = false;
	int from = SURFACE_ASPHALT;
	int to = SURFACE_ASPHALT;
};

// Watches one wheel's surface. One of these per corner — it deliberately does
// not hold an array, because `CORNER_COUNT` belongs to chassis_flex.h and a
// second copy of a single-owner constant is how the two drift apart.
//
// Contains no state the simulation reads back, so a replay is identical whether
// or not anything is listening. ARCHITECTURE.md §8.
class SurfaceWatcher {
public:
	// Feed this tick's surface; returns the change, if any.
	SurfaceChange update(int type) {
		SurfaceChange change;
		const int clamped = (type >= 0 && type < SURFACE_COUNT) ? type : SURFACE_ASPHALT;
		if (!_seen) {
			// The first sample is not a transition. Spawning a kart on asphalt
			// must not fire an "arrived on asphalt" event, or every consumer
			// starts its life with a burst of dust from nowhere.
			_seen = true;
			_current = clamped;
			return change;
		}
		if (clamped == _current) {
			return change;
		}
		change.changed = true;
		change.from = _current;
		change.to = clamped;
		_current = clamped;
		return change;
	}

	int current() const { return _current; }

	void reset() {
		_seen = false;
		_current = SURFACE_ASPHALT;
	}

private:
	bool _seen = false;
	int _current = SURFACE_ASPHALT;
};

} // namespace kart::core

#endif // KART_CORE_SURFACE_H
