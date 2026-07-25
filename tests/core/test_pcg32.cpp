#include "doctest.h"

#include "core/pcg32.h"

#include <map>
#include <vector>

// PCG32 is the only generator anything the simulation can see is allowed to use
// (ARCHITECTURE.md §8 rule 3), so what these tests protect is the *contract*:
// same seed and stream give the same sequence, different streams do not
// correlate, and a snapshot restores exactly. A replay that diverges after an
// engine upgrade is the failure §8 exists to prevent, and this is the half of it
// that can be tested without an engine.

using kart::core::Pcg32;

static std::vector<uint32_t> draw(Pcg32 &generator, int count) {
	std::vector<uint32_t> values;
	values.reserve(count);
	for (int index = 0; index < count; ++index) {
		values.push_back(generator.next_uint32());
	}
	return values;
}

TEST_CASE("the same seed and stream give the same sequence") {
	Pcg32 first(42, 7);
	Pcg32 second(42, 7);
	CHECK(draw(first, 64) == draw(second, 64));
}

TEST_CASE("a different stream is a different sequence from the same seed") {
	Pcg32 gameplay(42, 1);
	Pcg32 presentation(42, 2);
	const auto a = draw(gameplay, 64);
	const auto b = draw(presentation, 64);
	CHECK(a != b);

	// Not merely different — uncorrelated enough that no draw lines up. Two
	// streams that agreed on even one index in 64 would be a real finding: it is
	// the property rule 3 relies on when it says a particle spawn must not
	// disturb a gameplay generator.
	int matches = 0;
	for (size_t index = 0; index < a.size(); ++index) {
		if (a[index] == b[index]) {
			++matches;
		}
	}
	CHECK(matches == 0);
}

TEST_CASE("every stream selector produces an odd increment") {
	// The increment must be odd or the LCG's period halves — a generator that
	// still looks random and covers half the states it should. `reseed` forces it
	// with `(stream << 1) | 1`, which is injective, so adjacent stream numbers
	// stay distinct sequences rather than colliding in pairs.
	for (uint64_t stream = 0; stream < 8; ++stream) {
		Pcg32 generator(1, stream);
		CHECK(generator.increment() % 2 == 1);
	}
	Pcg32 even(1, 2);
	Pcg32 odd(1, 3);
	CHECK(even.increment() != odd.increment());
	CHECK(draw(even, 16) != draw(odd, 16));
}

TEST_CASE("restore forces the increment odd as well") {
	// A serialized increment that lost its low bit somewhere would otherwise
	// produce a subtly worse generator rather than an error.
	Pcg32 generator;
	generator.restore(12345, 1000);
	CHECK(generator.increment() % 2 == 1);
}

TEST_CASE("snapshot and restore reproduce the tail of a sequence") {
	Pcg32 generator(2024, 5);
	draw(generator, 100);

	const uint64_t state = generator.state();
	const uint64_t increment = generator.increment();
	const auto expected = draw(generator, 32);

	Pcg32 restored;
	restored.restore(state, increment);
	CHECK(draw(restored, 32) == expected);
}

TEST_CASE("next_double stays inside the unit interval") {
	Pcg32 generator(9, Pcg32::DEFAULT_STREAM);
	double low = 1.0;
	double high = 0.0;
	for (int index = 0; index < 20000; ++index) {
		const double value = generator.next_double();
		CHECK(value >= 0.0);
		CHECK(value < 1.0);
		low = value < low ? value : low;
		high = value > high ? value : high;
	}
	// Over 20,000 draws the extremes should be close to the ends. A generator
	// stuck in a narrow band passes every "is it in range" check ever written.
	CHECK(low < 0.001);
	CHECK(high > 0.999);
}

TEST_CASE("next_below is uniform and never reaches its bound") {
	Pcg32 generator(11, 3);
	std::map<uint32_t, int> counts;
	const int draws = 60000;
	const uint32_t bound = 6;
	for (int index = 0; index < draws; ++index) {
		const uint32_t value = generator.next_below(bound);
		CHECK(value < bound);
		counts[value] += 1;
	}
	// A modulo-biased generator over a bound that does not divide 2^32 shows up
	// as a few percent of extra weight on the low values. The expected count is
	// 10,000 per bucket; 3% is far tighter than the bias would be and far looser
	// than sampling noise.
	for (const auto &bucket : counts) {
		CHECK(bucket.second > 9700);
		CHECK(bucket.second < 10300);
	}
	CHECK(counts.size() == bound);
}

TEST_CASE("a zero bound is answered rather than divided by") {
	Pcg32 generator(1, 1);
	CHECK(generator.next_below(0) == 0);
}

TEST_CASE("next_range spans its endpoints") {
	Pcg32 generator(77, 4);
	for (int index = 0; index < 5000; ++index) {
		const double value = generator.next_range(-2.5, 7.5);
		CHECK(value >= -2.5);
		CHECK(value < 7.5);
	}
}

TEST_CASE("the reference constants are the reference constants") {
	// O'Neill's multiplier. If someone "optimizes" this the generator still looks
	// random and stops being the one whose equidistribution was proven.
	CHECK(Pcg32::MULTIPLIER == 6364136223846793005ULL);
}
