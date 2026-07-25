#include "kart_random.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

using namespace godot;

namespace kartgame {

void KartRandom::_bind_methods() {
	ClassDB::bind_method(D_METHOD("seed", "seed", "stream"), &KartRandom::seed);
	ClassDB::bind_method(D_METHOD("next_uint32"), &KartRandom::next_uint32);
	ClassDB::bind_method(D_METHOD("next_below", "bound"), &KartRandom::next_below);
	ClassDB::bind_method(D_METHOD("next_float"), &KartRandom::next_float);
	ClassDB::bind_method(D_METHOD("next_range", "low", "high"), &KartRandom::next_range);
	ClassDB::bind_method(D_METHOD("snapshot"), &KartRandom::snapshot);
	ClassDB::bind_method(D_METHOD("restore", "state", "increment"), &KartRandom::restore);
}

void KartRandom::seed(int64_t p_seed, int64_t p_stream) {
	generator_.reseed(static_cast<uint64_t>(p_seed), static_cast<uint64_t>(p_stream));
}

int64_t KartRandom::next_uint32() {
	// Widened to 64-bit on the way out, so a full 32-bit draw is never delivered
	// to GDScript as a negative number.
	return static_cast<int64_t>(generator_.next_uint32());
}

int64_t KartRandom::next_below(int64_t p_bound) {
	if (p_bound <= 0) {
		return 0;
	}
	// Bounds above 2^32 are not representable by the underlying draw. Reported by
	// clamping rather than by silently wrapping, which would produce a
	// distribution nobody asked for.
	const uint32_t bound = p_bound > 0xFFFFFFFF ? 0xFFFFFFFFU : static_cast<uint32_t>(p_bound);
	return static_cast<int64_t>(generator_.next_below(bound));
}

double KartRandom::next_float() {
	return generator_.next_double();
}

double KartRandom::next_range(double p_low, double p_high) {
	return generator_.next_range(p_low, p_high);
}

Array KartRandom::snapshot() const {
	Array state;
	state.push_back(static_cast<int64_t>(generator_.state()));
	state.push_back(static_cast<int64_t>(generator_.increment()));
	return state;
}

void KartRandom::restore(int64_t p_state, int64_t p_increment) {
	generator_.restore(static_cast<uint64_t>(p_state), static_cast<uint64_t>(p_increment));
}

} // namespace kartgame
