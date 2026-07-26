#include "doctest.h"

#include "core/surface.h"
#include "core/kz_reference.h"
#include "core/tire.h"
#include "core/units.h"

// The surface table's only interesting claim is what it does to the kart, so
// almost nothing here asserts a number in surface.h against itself. What is
// measured instead is the lateral acceleration a kart on four loaded tires can
// hold on each surface, computed through tire.h — which is the number issue
// #42's "driving onto grass loses grip immediately and obviously" is actually
// about, and the number that would change silently if either file drifted.
//
// The rest is the wire format. `SurfaceType`'s integers go into `track.json` at
// M5 and into anything that records where a wheel was, so they are pinned here
// by value rather than by name: a renumbering that a compiler is happy with is
// exactly the kind that reaches a saved track.

using kart::core::G;
using kart::core::kz::MASS_WITH_DRIVER_KG;
using kart::core::Surface;
using kart::core::SurfaceChange;
using kart::core::SurfaceWatcher;
using kart::core::Tire;
using kart::core::TireSlip;

// The lateral acceleration, in g, that four tires can hold on a surface.
//
// Steady state, level, no load transfer: the weight is split four ways and every
// tire is asked for pure lateral force at the slip angle where its curve peaks.
// Load transfer would lower this a little — that is tire.h's load sensitivity
// doing its job and it is tested there — so this is the envelope, which is the
// right thing to compare surfaces on.
//
// Written out through `Tire::evaluate` rather than as `peak_friction * grip` so
// that it goes through the friction ellipse and the surface multiplier the way
// the solver will.
static double lateral_g_on(int surface_type) {
	const Tire tire;
	const double weight = MASS_WITH_DRIVER_KG * G;

	TireSlip slip;
	slip.normal_load = weight / 4.0;
	slip.slip_angle = tire.lateral.peak_slip();
	slip.slip_ratio = 0.0;
	slip.surface_grip = kart::core::grip_for(surface_type);

	const double per_tire = tire.evaluate(slip).lateral;
	return per_tire * 4.0 / weight;
}

TEST_CASE("the surface values are a wire format and are pinned") {
	// Written as literals on purpose. Comparing the enum to itself would pass
	// through any renumbering; these are the numbers M5's track.json will
	// contain, and asphalt has to be zero so that a default `GroundQuery`
	// describes the track.
	CHECK(kart::core::SURFACE_ASPHALT == 0);
	CHECK(kart::core::SURFACE_CURB == 1);
	CHECK(kart::core::SURFACE_GRASS == 2);
	CHECK(kart::core::SURFACE_DIRT == 3);
	CHECK(kart::core::SURFACE_COUNT == 4);
}

TEST_CASE("every row sits at its own index and names itself") {
	for (int type = 0; type < kart::core::SURFACE_COUNT; ++type) {
		const Surface &row = kart::core::surface(type);
		// The row carries its own value, so a table reordered without its enum
		// fails here instead of in a replay.
		CHECK(row.type == type);
		CHECK(row.name != nullptr);
		CHECK(row.name[0] != '\0');
		CHECK(row.scrub_audio != nullptr);
		CHECK(row.particle_effect != nullptr);
		CHECK(row.grip > 0.0);
		CHECK(row.grip <= 1.0);
		CHECK(row.roughness >= 0.0);
		CHECK(row.roughness <= 1.0);
	}
}

TEST_CASE("an unknown surface value falls back to asphalt, not off the end") {
	// A track authored against a later build, or a replay from one. Both arrive
	// as an integer this build has never seen, and neither is a reason to stop
	// driving.
	CHECK(kart::core::surface(-1).type == kart::core::SURFACE_ASPHALT);
	CHECK(kart::core::surface(4).type == kart::core::SURFACE_ASPHALT);
	CHECK(kart::core::surface(1 << 20).type == kart::core::SURFACE_ASPHALT);
	CHECK(kart::core::grip_for(-7) == doctest::Approx(1.0));
}

TEST_CASE("asphalt is the anchor and is exactly one") {
	// Not a tuning value. tire.h's 2.10 already describes a kart slick on a hot
	// racing surface, so the table's job is to say what is left elsewhere. If
	// this is ever not 1.0, two files are describing the same thing.
	CHECK(kart::core::grip_for(kart::core::SURFACE_ASPHALT) == doctest::Approx(1.0));
}

TEST_CASE("the multipliers are the ones the references derive") {
	// docs/REFERENCES.md's surfaces section is where these come from; the point
	// of restating them is that changing one without changing the other is a
	// test failure rather than a quiet divergence between a number and its
	// justification.
	CHECK(kart::core::grip_for(kart::core::SURFACE_CURB) == doctest::Approx(0.72));
	CHECK(kart::core::grip_for(kart::core::SURFACE_GRASS) == doctest::Approx(0.18));
	CHECK(kart::core::grip_for(kart::core::SURFACE_DIRT) == doctest::Approx(0.17));
}

TEST_CASE("asphalt holds the ARCHITECTURE 6.4 lateral envelope") {
	const double asphalt = lateral_g_on(kart::core::SURFACE_ASPHALT);
	// Checked against the **peak** band, not the sustained one, and the reason is
	// that this figure is not a vehicle measurement at all: `lateral_g_on` loads
	// four tires equally and asks the curve what it gives, with no load transfer,
	// no rollover and no chassis. It is a tire bench number that happens to be
	// expressed in g. Against the sustained band it would fail outright, and it
	// should not — nothing here is sustaining anything.
	//
	// The honest home for it is `tire.peak_friction` rather than any vehicle
	// band; issue #128 tracks that, together with the larger problem that
	// `peak_friction` itself was justified by pointing at §6.4 and every surface
	// multiplier below is a quotient of it. Until then this at least asserts
	// against the band that describes the right kind of quantity.
	//
	// It is repeated here rather than left to test_tire.cpp because every other
	// surface in this file is measured as a fraction of it, and a table of
	// fractions of a wrong number is worse than no table.
	CHECK(asphalt > kart::core::kz::LATERAL_PEAK_G_MIN);
	CHECK(asphalt < kart::core::kz::LATERAL_PEAK_G_MAX);
	CHECK(asphalt == doctest::Approx(2.13).epsilon(0.01));
}

TEST_CASE("driving onto grass loses grip immediately and obviously") {
	const double asphalt = lateral_g_on(kart::core::SURFACE_ASPHALT);
	const double grass = lateral_g_on(kart::core::SURFACE_GRASS);

	// 0.38 g. A kart that was holding 2.1 g keeps under a fifth of it, which is
	// issue #42's acceptance criterion turned into a number: a driver who puts
	// two wheels on the grass mid-corner has lost the corner, not a bit of pace.
	CHECK(grass == doctest::Approx(0.383).epsilon(0.02));
	CHECK(asphalt / grass > 5.0);

	// And it is still a surface, not ice. A kart on grass can be steered slowly
	// back onto the track; the model must not make the run-off a death sentence,
	// because ARCHITECTURE.md §17's race modes have to survive a mistake.
	CHECK(grass > 0.3);
}

TEST_CASE("curbs are distinct from both asphalt and grass") {
	const double asphalt = lateral_g_on(kart::core::SURFACE_ASPHALT);
	const double curb = lateral_g_on(kart::core::SURFACE_CURB);
	const double grass = lateral_g_on(kart::core::SURFACE_GRASS);

	// 1.53 g: a curb gives away real grip and remains a surface a kart races on.
	CHECK(curb == doctest::Approx(1.53).epsilon(0.02));
	// The margins issue #42's second acceptance item asks for, stated as
	// multiples so that they survive a retune of the tire.
	CHECK(asphalt / curb > 1.3);
	CHECK(curb / grass > 3.5);
}

TEST_CASE("grass and dirt land within a tenth of each other, as the literature says") {
	const double grass = lateral_g_on(kart::core::SURFACE_GRASS);
	const double dirt = lateral_g_on(kart::core::SURFACE_DIRT);
	// Cenek et al. write "the coefficient of dry rye-grass is comparable to
	// gravel" in as many words, and the two derivations land 6% apart. This
	// asserts the agreement rather than hiding it: if someone later separates
	// them by feel, this test is where the reason has to be written down.
	CHECK(dirt < grass);
	CHECK(grass / dirt < 1.1);
}

TEST_CASE("the surface multiplier scales longitudinal grip too") {
	// `TireSlip::surface_grip` multiplies both axes, so braking on grass has to
	// fall by the same factor cornering does. A surface that only affected
	// lateral force would let a kart brake from 140 km/h on a lawn.
	const Tire tire;
	TireSlip slip;
	slip.normal_load = MASS_WITH_DRIVER_KG * G / 4.0;
	slip.slip_ratio = -tire.longitudinal.peak_slip();

	slip.surface_grip = 1.0;
	const double asphalt = tire.evaluate(slip).longitudinal;
	slip.surface_grip = kart::core::grip_for(kart::core::SURFACE_GRASS);
	const double grass = tire.evaluate(slip).longitudinal;

	CHECK(grass / asphalt == doctest::Approx(0.18).epsilon(0.001));
}

TEST_CASE("utilization is reported against the surface, not against asphalt") {
	// The friction ellipse is scaled by the surface, so a tire asked for a force
	// it could have made on asphalt reports utilization above 1 on grass. That
	// is what makes the telemetry panel's utilization trace and §12's scrub audio
	// say something true when the kart leaves the track.
	const Tire tire;
	TireSlip slip;
	slip.normal_load = MASS_WITH_DRIVER_KG * G / 4.0;
	slip.slip_angle = tire.lateral.peak_slip();
	slip.surface_grip = kart::core::grip_for(kart::core::SURFACE_GRASS);

	const kart::core::TireForce force = tire.evaluate(slip);
	// At the peak of the curve the tire is using all of what this surface has.
	CHECK(force.utilization == doctest::Approx(1.0).epsilon(0.001));
	// And what it has is 165 N against a 429 N wheel load — under a fifth of the
	// 914 N the same tire makes on asphalt.
	CHECK(force.lateral == doctest::Approx(164.5).epsilon(0.01));
	CHECK(force.lateral < slip.normal_load * 0.4);
}

TEST_CASE("a wheel off the ground produces nothing on any surface") {
	const Tire tire;
	for (int type = 0; type < kart::core::SURFACE_COUNT; ++type) {
		TireSlip slip;
		slip.normal_load = 0.0;
		slip.slip_angle = 0.2;
		slip.surface_grip = kart::core::grip_for(type);
		const kart::core::TireForce force = tire.evaluate(slip);
		CHECK(force.lateral == doctest::Approx(0.0));
		CHECK(force.longitudinal == doctest::Approx(0.0));
	}
}

TEST_CASE("the curb ripple is a row of bumps, not a groove") {
	const Surface &curb = kart::core::surface(kart::core::SURFACE_CURB);
	REQUIRE(curb.ripple_wavelength > 0.0);

	// Never negative: a rectified cosine stands on the curb's top face. A plain
	// sine would cut into the concrete for half of every cycle, and the M5 mesh
	// generated from it would have holes in it.
	for (int step = 0; step <= 400; ++step) {
		const double distance = curb.ripple_wavelength * 4.0 * double(step) / 400.0;
		const double height = kart::core::ripple_height(curb, distance);
		CHECK(height >= 0.0);
		CHECK(height <= curb.ripple_amplitude + 1e-12);
	}

	// Zero at the start of every cycle, peak at the half.
	CHECK(kart::core::ripple_height(curb, 0.0) == doctest::Approx(0.0));
	CHECK(kart::core::ripple_height(curb, curb.ripple_wavelength) == doctest::Approx(0.0));
	CHECK(kart::core::ripple_height(curb, curb.ripple_wavelength * 0.5)
			== doctest::Approx(curb.ripple_amplitude));

	// The dimensions the M5 mesh has to be generated at. Assumed rather than
	// sourced — see surface.h — so they are pinned here to make a later change
	// deliberate.
	CHECK(curb.ripple_amplitude == doctest::Approx(0.012));
	CHECK(curb.ripple_wavelength == doctest::Approx(0.15));
}

TEST_CASE("a curb ripple is long enough for a tire to follow") {
	// The wavelength is anchored to the rear slick's contact chord: a tire
	// bridges a ripple much shorter than the patch it stands on instead of
	// following it. 0.1475 m loaded radius at about 2 mm of deflection gives a
	// chord of 2*sqrt(2*R*d). The wheel radius belongs to chassis.h, which
	// src/core/ can include but this test deliberately does not: what is being
	// checked is the relationship, and restating the radius here would create a
	// second copy of a single-owner number.
	const double rear_radius = 0.1475;
	const double deflection = 0.002;
	const double chord = 2.0 * std::sqrt(2.0 * rear_radius * deflection);
	const Surface &curb = kart::core::surface(kart::core::SURFACE_CURB);
	CHECK(curb.ripple_wavelength > chord * 2.0);
}

TEST_CASE("flat surfaces have no ripple at all") {
	for (int type = 0; type < kart::core::SURFACE_COUNT; ++type) {
		if (type == kart::core::SURFACE_CURB) {
			continue;
		}
		const Surface &row = kart::core::surface(type);
		CHECK(row.ripple_amplitude == doctest::Approx(0.0));
		CHECK(kart::core::ripple_height(row, 0.37) == doctest::Approx(0.0));
	}
}

TEST_CASE("only the deformable surfaces are marked deformable") {
	// The flag is what the grip column's derivation turns on — an absolute
	// coefficient for terrain that shears, a ratio for rubber on stone — and
	// M10's particle hook reads the same bit, because a deformable surface is
	// the one that throws material.
	CHECK_FALSE(kart::core::surface(kart::core::SURFACE_ASPHALT).deformable);
	CHECK_FALSE(kart::core::surface(kart::core::SURFACE_CURB).deformable);
	CHECK(kart::core::surface(kart::core::SURFACE_GRASS).deformable);
	CHECK(kart::core::surface(kart::core::SURFACE_DIRT).deformable);
}

TEST_CASE("every surface off the track has somewhere to hang audio and particles") {
	// Issue #42's third acceptance item: M8 and M10 must be able to find a bank
	// and an emitter without the solver knowing either exists. Asphalt is
	// deliberately empty — it is the default case, and a rolling loop for it
	// belongs to the engine note in §12 rather than to this table.
	CHECK(kart::core::surface(kart::core::SURFACE_ASPHALT).scrub_audio[0] == '\0');
	for (int type = 1; type < kart::core::SURFACE_COUNT; ++type) {
		const Surface &row = kart::core::surface(type);
		CHECK(row.scrub_audio[0] != '\0');
		CHECK(row.particle_effect[0] != '\0');
	}
}

TEST_CASE("the first surface a wheel sees is not a transition") {
	// Spawning on asphalt must not fire "arrived on asphalt", or every consumer
	// starts with a burst of dust from nowhere.
	SurfaceWatcher watcher;
	const SurfaceChange first = watcher.update(kart::core::SURFACE_ASPHALT);
	CHECK_FALSE(first.changed);
	CHECK(watcher.current() == kart::core::SURFACE_ASPHALT);

	SurfaceWatcher spawned_on_dirt;
	CHECK_FALSE(spawned_on_dirt.update(kart::core::SURFACE_DIRT).changed);
	CHECK(spawned_on_dirt.current() == kart::core::SURFACE_DIRT);
}

TEST_CASE("a surface change fires exactly once, with both ends of it") {
	SurfaceWatcher watcher;
	watcher.update(kart::core::SURFACE_ASPHALT);

	for (int tick = 0; tick < 10; ++tick) {
		CHECK_FALSE(watcher.update(kart::core::SURFACE_ASPHALT).changed);
	}

	const SurfaceChange onto_curb = watcher.update(kart::core::SURFACE_CURB);
	CHECK(onto_curb.changed);
	CHECK(onto_curb.from == kart::core::SURFACE_ASPHALT);
	CHECK(onto_curb.to == kart::core::SURFACE_CURB);

	// Held, not re-fired. A hiss loop that restarted every tick is the failure
	// this exists to prevent.
	for (int tick = 0; tick < 10; ++tick) {
		CHECK_FALSE(watcher.update(kart::core::SURFACE_CURB).changed);
	}

	const SurfaceChange onto_grass = watcher.update(kart::core::SURFACE_GRASS);
	CHECK(onto_grass.changed);
	CHECK(onto_grass.from == kart::core::SURFACE_CURB);
	CHECK(onto_grass.to == kart::core::SURFACE_GRASS);
}

TEST_CASE("a watcher fed an unknown surface reports asphalt rather than the raw value") {
	// Same reasoning as the table lookup: an unrecognized value from a later
	// build must not become a surface id that no audio bank matches.
	SurfaceWatcher watcher;
	watcher.update(kart::core::SURFACE_GRASS);
	const SurfaceChange change = watcher.update(99);
	CHECK(change.changed);
	CHECK(change.to == kart::core::SURFACE_ASPHALT);
	CHECK(watcher.current() == kart::core::SURFACE_ASPHALT);
}

TEST_CASE("a reset watcher does not fire on the surface it is put back on") {
	// Respawn. The kart is placed on the track and the first tick after that is
	// not an arrival.
	SurfaceWatcher watcher;
	watcher.update(kart::core::SURFACE_GRASS);
	watcher.reset();
	CHECK_FALSE(watcher.update(kart::core::SURFACE_ASPHALT).changed);
}
