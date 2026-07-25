#ifndef KARTGAME_KART_RANDOM_H
#define KARTGAME_KART_RANDOM_H

#include "core/pcg32.h"

#include <godot_cpp/classes/ref_counted.hpp>

namespace kartgame {

// GDScript's handle on `kart::core::Pcg32`.
//
// It exists so that ARCHITECTURE.md §8 rule 3 — gameplay RNG and presentation
// RNG are separate explicitly seeded streams — is enforceable from GDScript as
// well as from the solver. Godot's own `RandomNumberGenerator` is the thing this
// replaces for anything the sim can see: its internal state is not ours to pin
// across engine versions, and a replay that diverges after an engine upgrade is
// the exact failure §8 exists to prevent. Particles and other presentation-only
// noise may keep using the engine's generator; that is the point of the split.
//
// Every instance must be seeded deliberately. There is no time-based
// `randomize()` here on purpose — an unseeded generator is a bug in a
// deterministic sim, so the API makes it impossible to write by accident rather
// than easy to write and hard to notice.
class KartRandom : public godot::RefCounted {
	GDCLASS(KartRandom, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// `stream` selects an independent sequence. Two generators with the same seed
	// and different streams do not correlate, which is what lets each system own
	// one without coordinating seeds.
	void seed(int64_t p_seed, int64_t p_stream);

	int64_t next_uint32();
	int64_t next_below(int64_t p_bound);
	double next_float();
	double next_range(double p_low, double p_high);

	// Snapshot and restore, for putting a generator into a replay. Both halves of
	// the state are needed: the increment identifies the stream, the state
	// identifies the position in it.
	godot::Array snapshot() const;
	void restore(int64_t p_state, int64_t p_increment);

private:
	kart::core::Pcg32 generator_;
};

} // namespace kartgame

#endif // KARTGAME_KART_RANDOM_H
