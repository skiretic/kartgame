#include "doctest.h"

#include "core/chassis.h"
#include "core/kz_reference.h"

// The KZ envelope is a plausibility gate, not a fit target, so what is worth
// asserting is that the gate is *shaped* like a gate: every range the right way
// round, and every figure inside the class it claims to describe.
//
// `verify_extension.gd` checks the same invariants through the GDExtension
// boundary. That is not duplication — it checks that the marshalling preserves
// them, which is a different failure. This checks the constants themselves, with
// no engine in the process at all, which is the point of ADR-0017.

namespace kz = kart::core::kz;

TEST_CASE("every range is the right way round") {
	CHECK(kz::TOP_SPEED_MIN_KMH < kz::TOP_SPEED_MAX_KMH);
	CHECK(kz::ZERO_TO_100_KMH_MIN_S < kz::ZERO_TO_100_KMH_MAX_S);
	CHECK(kz::LATERAL_SUSTAINED_G_MIN < kz::LATERAL_SUSTAINED_G_MAX);
	CHECK(kz::LATERAL_PEAK_G_MIN < kz::LATERAL_PEAK_G_MAX);
	// A transient peak is by definition not below what the kart can hold all day.
	CHECK(kz::LATERAL_SUSTAINED_G_MAX <= kz::LATERAL_PEAK_G_MIN);
	CHECK(kz::BRAKING_G_MIN < kz::BRAKING_G_MAX);
	CHECK(kz::POWERBAND_MIN_RPM < kz::POWERBAND_MAX_RPM);
}

TEST_CASE("peak power falls inside the usable powerband") {
	// A peak outside the band would make every gear-selection decision in M3b
	// and M7 nonsense, and it is the kind of transposition that survives review.
	CHECK(kz::PEAK_POWER_RPM >= kz::POWERBAND_MIN_RPM);
	CHECK(kz::PEAK_POWER_RPM <= kz::POWERBAND_MAX_RPM);
}

TEST_CASE("the class figures are the class figures") {
	// 175 kg and six gears are regulation, not tuning. ADR-0011 chose KZ over a
	// single-speed TaG precisely because the gearbox is the interesting part.
	CHECK(kz::MASS_WITH_DRIVER_KG == doctest::Approx(175.0));
	CHECK(kz::GEAR_COUNT == 6);
	CHECK(kz::PEAK_POWER_HP == doctest::Approx(45.0));
}

TEST_CASE("the derived top speed agrees with its own conversion") {
	// `TOP_SPEED_MAX_MS` exists so the conversion is exercised rather than
	// hand-copied. This asserts it stayed derived.
	CHECK(kz::TOP_SPEED_MAX_MS == doctest::Approx(kz::TOP_SPEED_MAX_KMH / 3.6));
	CHECK(kz::TOP_SPEED_MAX_MS == doctest::Approx(40.2777778));
}

TEST_CASE("the envelope is internally consistent") {
	// Reaching 100 km/h in the quoted time needs at least the average
	// acceleration that implies, and a kart that could brake harder than it can
	// corner would be describing a car with wings. Neither is a deep truth; both
	// catch a figure edited in isolation.
	const double average_accel = kart::core::kmh_to_ms(100.0) / kz::ZERO_TO_100_KMH_MAX_S;
	CHECK(kart::core::ms2_to_g(average_accel) < kz::LATERAL_SUSTAINED_G_MIN);
	// Braking against the peak band, because §6.4 labels the braking row "peak"
	// too. Issue #131 notes that the figure actually measured against it is a
	// 90-20 km/h *mean*, which is the same category error this file just had
	// fixed in the lateral row — filed rather than fixed here, because unlike the
	// lateral case it is a labeling problem and not a physical impossibility.
	CHECK(kz::BRAKING_G_MAX <= kz::LATERAL_PEAK_G_MAX);
}

TEST_CASE("the sustained lateral band is one the kart can actually occupy") {
	// **This is the assertion whose absence cost two milestones.** ADR-0034: the
	// project carried a single 2.0-2.5 g lateral band, labeled "steady-state
	// skidpad" in this file and "peak lateral acceleration" in ARCHITECTURE.md
	// §6.4, and asserted it against sustained measurements. Nobody noticed,
	// because nothing ever compared the band against the kart it described.
	//
	// A kart cannot sustain more lateral acceleration than it tips at. So the
	// sustained ceiling must sit below the rollover threshold in the *worse* of
	// the two directions — which is turning left, because 27 kg of engine,
	// exhaust and radiator put the center of mass 41 mm right of the centerline.
	// The old band's top, 2.5 g, was above it. This one is not, and if anybody
	// widens it back it fails here rather than two milestones later.
	const kart::core::MassProperties properties = kz::kart_mass_properties();
	const double worst = kz::rollover_threshold_g(properties, true);

	CHECK(worst == doctest::Approx(2.434).epsilon(1e-2));
	CHECK(kz::LATERAL_SUSTAINED_G_MAX < worst);

	// With real margin, not by a rounding error. A band whose ceiling sits at
	// 99% of the tipping point describes a kart balanced on two wheels.
	CHECK(worst - kz::LATERAL_SUSTAINED_G_MAX > 0.3);

	// The peak band is deliberately allowed above it — tipping takes time, and
	// 2.5 g needs 2.6 s to actually put this kart over. That asymmetry is the
	// whole reason the two bands exist separately.
	CHECK(kz::LATERAL_PEAK_G_MAX > worst);
}
