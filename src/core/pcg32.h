#ifndef KART_CORE_PCG32_H
#define KART_CORE_PCG32_H

#include <cstdint>

// PCG32, the project's only random number generator.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ARCHITECTURE.md §8 rule 3 names PCG32 explicitly and requires that every user
// carry its own explicitly seeded stream, because a shared generator makes
// gameplay depend on how many particles were spawned. The engine's own
// `RandomNumberGenerator` is deliberately not used for anything the sim can see:
// it is a PCG variant too, but its state is not ours to pin across engine
// versions, and a replay that diverges after an engine upgrade is exactly the
// failure §8 exists to prevent.
//
// Why PCG32 and not xorshift or a Mersenne Twister: it is 8 bytes of state, it
// has a *stream* selector (two generators seeded identically but on different
// streams produce independent sequences, which is what rule 3 needs), it passes
// TestU01 BigCrush, and it is short enough to read in one sitting. The algorithm
// is O'Neill's, 2014; the constants below are hers and must not be "improved".
//
// The sequence is a pure function of (seed, stream, draw count). No wall-clock,
// no entropy source, no global state — the same three numbers always give the
// same stream, on any platform, which is what makes a replay reproducible.

namespace kart::core {

class Pcg32 {
public:
	// The multiplier from the reference implementation. Any other value breaks the
	// generator's proven equidistribution.
	static constexpr uint64_t MULTIPLIER = 6364136223846793005ULL;

	// A default stream, for the many call sites that only need one generator and
	// do not care which stream it is on. Distinct streams still have to be chosen
	// deliberately — that is the point of rule 3.
	static constexpr uint64_t DEFAULT_STREAM = 1442695040888963407ULL;

	constexpr Pcg32() :
			Pcg32(0, DEFAULT_STREAM) {}

	constexpr Pcg32(uint64_t seed, uint64_t stream) {
		reseed(seed, stream);
	}

	// Restart the stream. The odd-forcing shift on `stream` is required: the
	// increment must be odd for the LCG to have full period, and silently
	// accepting an even one would halve the period of a generator that looked
	// fine.
	constexpr void reseed(uint64_t seed, uint64_t stream) {
		state_ = 0U;
		increment_ = (stream << 1U) | 1U;
		next_uint32();
		state_ += seed;
		next_uint32();
	}

	// The generator proper: an LCG step, then a permutation of the *old* state.
	// Hashing the pre-step state is what separates PCG from a bare LCG, and it is
	// why the low bits are usable.
	constexpr uint32_t next_uint32() {
		const uint64_t previous = state_;
		state_ = previous * MULTIPLIER + increment_;
		const uint32_t xorshifted = static_cast<uint32_t>(((previous >> 18U) ^ previous) >> 27U);
		const uint32_t rotation = static_cast<uint32_t>(previous >> 59U);
		return (xorshifted >> rotation) | (xorshifted << ((~rotation + 1U) & 31U));
	}

	// Uniform in [0, bound), without the modulo bias that `% bound` would have.
	// The rejection threshold is the largest multiple of `bound` below 2^32, and
	// draws below it are discarded. Loops at most a handful of times, and the
	// expected number of extra draws is under one for any bound worth using.
	constexpr uint32_t next_below(uint32_t bound) {
		if (bound == 0) {
			return 0;
		}
		const uint32_t threshold = (~bound + 1U) % bound;
		for (;;) {
			const uint32_t draw = next_uint32();
			if (draw >= threshold) {
				return draw % bound;
			}
		}
	}

	// Uniform in [0, 1). Built by scaling rather than by stuffing bits into a
	// mantissa, so the result is exactly representable and the same on every
	// platform: 2^-32 is exact in both float and double.
	constexpr double next_double() {
		return static_cast<double>(next_uint32()) * (1.0 / 4294967296.0);
	}

	constexpr double next_range(double low, double high) {
		return low + (high - low) * next_double();
	}

	// The full internal state, so a generator can be snapshotted into a replay and
	// restored exactly. Two fields rather than one, because the increment is part
	// of the identity of the stream, not of its position in it.
	constexpr uint64_t state() const { return state_; }
	constexpr uint64_t increment() const { return increment_; }

	constexpr void restore(uint64_t state, uint64_t increment) {
		state_ = state;
		// Same odd-forcing rule as `reseed`: a restored increment that lost its low
		// bit somewhere in serialization would produce a subtly worse generator
		// rather than an error.
		increment_ = increment | 1U;
	}

private:
	uint64_t state_ = 0;
	uint64_t increment_ = DEFAULT_STREAM;
};

} // namespace kart::core

#endif // KART_CORE_PCG32_H
