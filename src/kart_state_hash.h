#ifndef KARTGAME_KART_STATE_HASH_H
#define KARTGAME_KART_STATE_HASH_H

#include "core/state_hash.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace kartgame {

// GDScript's handle on `kart::core::StateHash`.
//
// An accumulator rather than a hash-this-array function, because that is the
// shape the caller needs: ARCHITECTURE.md §8 hashes sim state every N ticks
// across every body in the scene, and building an intermediate array of every
// float first would allocate once per tick to hash a few hundred bytes.
//
// The engine-facing conveniences — `add_transform`, `add_vector3` — live here
// rather than in `src/core/`, because `Transform3D` is a godot-cpp type and
// ADR-0017 keeps `src/core/` free of it. The arithmetic they do is none: they
// unpack a Variant and hand doubles to the core class.
class KartStateHash : public godot::RefCounted {
	GDCLASS(KartStateHash, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	void reset();

	// The quantization grid, in the unit of whatever is being hashed. Settable so
	// a test can tighten it deliberately; the default is the one the harness runs
	// with and the one `state_hash.h` explains.
	void set_quantum(double p_quantum);
	double get_quantum() const;

	void add_float(double p_value);
	void add_int(int64_t p_value);
	void add_vector3(const godot::Vector3 &p_value);

	// Position and basis together, which is what "where is this body" means. The
	// basis goes in column by column, so a transposed basis hashes differently —
	// a silently transposed rotation is a real bug class and a hash that hid it
	// would be worse than none.
	void add_transform(const godot::Transform3D &p_value);

	// Hex, not an int. GDScript's int is signed 64-bit, so half of the possible
	// digests would come back negative and every log line would need explaining.
	godot::String hex() const;

	// The raw digest for callers that want to compare rather than print. Wraps
	// past 2^63 into negative values; `hex()` is what to log.
	int64_t digest() const;

private:
	double quantum_ = kart::core::StateHash::DEFAULT_QUANTUM;
	kart::core::StateHash hash_{ kart::core::StateHash::DEFAULT_QUANTUM };
};

} // namespace kartgame

#endif // KARTGAME_KART_STATE_HASH_H
