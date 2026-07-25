#include "doctest.h"

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
	CHECK(kz::LATERAL_G_MIN < kz::LATERAL_G_MAX);
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
	CHECK(kart::core::ms2_to_g(average_accel) < kz::LATERAL_G_MIN);
	CHECK(kz::BRAKING_G_MAX <= kz::LATERAL_G_MAX);
}
