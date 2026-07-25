#ifndef KART_CORE_STATE_HASH_H
#define KART_CORE_STATE_HASH_H

#include <cstdint>
#include <cmath>

// Simulation state hashing — the determinism harness from ARCHITECTURE.md §8
// item 6 and ROADMAP M6.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## Why floats are quantized before hashing
//
// The obvious implementation hashes the raw bits of every float. It does not
// work, and the reason is worth stating because it will be re-proposed: a replay
// is re-simulated, not replayed from a recording, so the second run's arithmetic
// happens in a different order at the machine level — different register
// allocation, different FMA contraction, a different SIMD width for the same
// source. Values agree to within an ULP or two and disagree bit for bit, so a
// bitwise hash reports divergence on every tick and detects nothing.
//
// Quantizing to a fixed grid before hashing fixes that, at the cost of stating
// what "the same" means. The default grid is 1e-4: a tenth of a millimeter in
// position and 0.1 mm/s in velocity. A kart that has genuinely diverged crosses
// that within a handful of ticks — chaotic systems separate exponentially, which
// is the property that makes a coarse comparison sufficient rather than sloppy —
// while float noise never does.
//
// The documented limit in §8 still stands: same-binary reproduction is expected,
// cross-platform bit determinism is not. This makes the *same-binary* case
// robust; it does not make a different compiler produce the same lap.
//
// ## Why FNV-1a
//
// It is not the fastest hash and it is not the strongest. It is eleven lines,
// has no lookup table, no seed to lose, and no endianness question in the form
// used here, and the number of bytes hashed per tick is small enough that speed
// is irrelevant. A state hash that cannot be re-derived by hand from its own
// source is a state hash nobody will trust when it disagrees.

namespace kart::core {

class StateHash {
public:
	static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
	static constexpr uint64_t FNV_PRIME = 1099511628211ULL;

	// Grid the values are snapped to before hashing. See the header comment.
	static constexpr double DEFAULT_QUANTUM = 1e-4;

	// Hashed in place of a NaN. An arbitrary value with no other meaning; it
	// exists so that two NaNs hash alike and so that a NaN cannot be mistaken for
	// a plausible quantized integer.
	static constexpr uint64_t NAN_SENTINEL = 0x7FF8DEADBEEF0001ULL;

	constexpr explicit StateHash(double quantum = DEFAULT_QUANTUM) :
			quantum_(quantum) {}

	constexpr void add_uint64(uint64_t value) {
		for (int byte = 0; byte < 8; ++byte) {
			digest_ ^= static_cast<uint64_t>((value >> (byte * 8)) & 0xFFU);
			digest_ *= FNV_PRIME;
		}
	}

	constexpr void add_int(int64_t value) {
		add_uint64(static_cast<uint64_t>(value));
	}

	// Snap to the grid, then hash the integer. `llround` rather than a cast:
	// truncation would put -0.00005 and +0.00005 on opposite sides of zero while
	// rounding puts both at 0, and the sign of a value hovering at zero is exactly
	// the sort of thing that flickers between runs.
	void add_double(double value) {
		// A NaN would otherwise hash as an arbitrary integer and, worse, would
		// compare equal to another NaN with different bits. It is a bug wherever it
		// appears, so it gets its own sentinel and stays visible.
		if (std::isnan(value)) {
			add_uint64(NAN_SENTINEL);
			return;
		}
		add_int(static_cast<int64_t>(std::llround(value / quantum_)));
	}

	void add_vector3(double x, double y, double z) {
		add_double(x);
		add_double(y);
		add_double(z);
	}

	constexpr uint64_t digest() const { return digest_; }

	constexpr void reset() { digest_ = FNV_OFFSET_BASIS; }

private:
	uint64_t digest_ = FNV_OFFSET_BASIS;
	double quantum_ = DEFAULT_QUANTUM;
};

} // namespace kart::core

#endif // KART_CORE_STATE_HASH_H
