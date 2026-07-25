#include "doctest.h"

#include "core/units.h"

// `units.h` is arithmetic with no branches, so these tests are not looking for
// bugs in it — they are proving the harness runs at all, which is issue #25's
// stated reason for covering it first. What they do catch is a unit constant
// being "corrected" by someone who remembered a different one.

using namespace kart::core;

TEST_CASE("speed conversions round trip") {
	CHECK(kmh_to_ms(100.0) == doctest::Approx(27.7777777778));
	CHECK(ms_to_kmh(kmh_to_ms(140.0)) == doctest::Approx(140.0));

	// The §6.4 top-speed figures, in the unit the solver works in. A kart at
	// 140 km/h covers 38.9 m every second, which is the number that makes the
	// proving ground's size a requirement rather than a preference.
	CHECK(kmh_to_ms(140.0) == doctest::Approx(38.888888).epsilon(1e-5));
}

TEST_CASE("engine speed conversions round trip") {
	CHECK(rads_to_rpm(rpm_to_rads(13000.0)) == doctest::Approx(13000.0));
	// 13,000 rpm is peak power for a KZ. In rad/s that is a little over 1,361,
	// and a solver that ever reports "1361 rpm" has lost a conversion somewhere.
	CHECK(rpm_to_rads(13000.0) == doctest::Approx(1361.356816).epsilon(1e-6));
}

TEST_CASE("g conversions use standard gravity") {
	// Standard gravity, not 9.81. The difference is 0.03%, which is nothing in a
	// lap time and everything in a regression test that starts failing after
	// someone rounds it.
	CHECK(G == doctest::Approx(9.80665));
	CHECK(ms2_to_g(g_to_ms2(2.5)) == doctest::Approx(2.5));
	CHECK(g_to_ms2(1.0) == doctest::Approx(G));
}

TEST_CASE("conversions are usable at compile time") {
	// `constexpr` is load-bearing: the KZ reference block is built from these at
	// compile time, so a conversion that quietly stopped being constant would
	// break that rather than merely slow it down.
	static_assert(kmh_to_ms(36.0) == 10.0, "kmh_to_ms must be constexpr");
	static_assert(ms_to_kmh(10.0) == 36.0, "ms_to_kmh must be constexpr");
	CHECK(true);
}
