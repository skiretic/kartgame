#include "doctest.h"

#include "core/state_hash.h"

#include <cmath>
#include <limits>

// The state hash decides whether a replay reproduced, so its two properties are
// in tension and both need holding: it must ignore float noise, and it must not
// ignore anything else. These tests pin both edges — the largest difference it
// forgives and the smallest it reports.

using kart::core::StateHash;

static uint64_t hash_of(double value, double quantum = StateHash::DEFAULT_QUANTUM) {
	StateHash hash(quantum);
	hash.add_double(value);
	return hash.digest();
}

TEST_CASE("an empty hash is the FNV offset basis") {
	StateHash hash;
	CHECK(hash.digest() == StateHash::FNV_OFFSET_BASIS);
}

TEST_CASE("float noise below the quantum is forgiven") {
	// The case this exists for: a re-simulation lands an ULP or two away because
	// the compiler ordered the arithmetic differently. Hashed raw, that is a
	// divergence report on every tick and the harness detects nothing.
	const double position = 12.3456;
	CHECK(hash_of(position) == hash_of(position + 1e-9));
	CHECK(hash_of(position) == hash_of(std::nextafter(position, 1e9)));
}

TEST_CASE("a real divergence is reported") {
	// A tenth of a millimeter is the grid, so half of one is the largest error
	// that can hide. A kart that has genuinely diverged passes that within a few
	// ticks — chaotic systems separate exponentially, which is what makes a
	// coarse grid sufficient rather than sloppy.
	CHECK(hash_of(12.3456) != hash_of(12.3456 + 1e-3));
	CHECK(hash_of(0.0) != hash_of(1e-4));
}

TEST_CASE("rounding is symmetric about zero") {
	// Truncation would put -0.00005 and +0.00005 on opposite sides of zero. The
	// sign of a value hovering at zero is exactly what flickers between runs, so
	// it must not be what the hash keys on.
	CHECK(hash_of(-0.00004) == hash_of(0.00004));
	CHECK(hash_of(-0.0) == hash_of(0.0));
}

TEST_CASE("the quantum is honored") {
	// A coarser grid forgives more. Stated as a test because a scenario that
	// tightens the grid deliberately is a supported thing to do, and a quantum
	// that silently did nothing would make that a lie.
	CHECK(hash_of(1.0, 1.0) == hash_of(1.4, 1.0));
	CHECK(hash_of(1.0, 0.1) != hash_of(1.4, 0.1));
}

TEST_CASE("order matters") {
	// The hash is over a sequence, not a set. Two bodies that swapped places in
	// the iteration order have not produced the same simulation state, and
	// ARCHITECTURE.md §8 item 4 requires stable iteration order for that reason.
	StateHash forward;
	forward.add_double(1.0);
	forward.add_double(2.0);

	StateHash backward;
	backward.add_double(2.0);
	backward.add_double(1.0);

	CHECK(forward.digest() != backward.digest());
}

TEST_CASE("a vector hashes as its three components in order") {
	StateHash packed;
	packed.add_vector3(1.0, 2.0, 3.0);

	StateHash loose;
	loose.add_double(1.0);
	loose.add_double(2.0);
	loose.add_double(3.0);

	CHECK(packed.digest() == loose.digest());
}

TEST_CASE("NaN hashes to a sentinel rather than to an arbitrary integer") {
	// Two NaNs compare unequal in float arithmetic and have different bit
	// patterns, so without this a NaN would make a run non-reproducible against
	// itself. A NaN is a bug wherever it appears; the hash's job is to be
	// consistent about it, not to hide it.
	const double first = std::numeric_limits<double>::quiet_NaN();
	const double second = -std::numeric_limits<double>::quiet_NaN();
	CHECK(hash_of(first) == hash_of(second));
	CHECK(hash_of(first) != hash_of(0.0));
}

TEST_CASE("reset returns the accumulator to its starting state") {
	StateHash hash;
	hash.add_double(3.14159);
	hash.add_int(42);
	CHECK(hash.digest() != StateHash::FNV_OFFSET_BASIS);
	hash.reset();
	CHECK(hash.digest() == StateHash::FNV_OFFSET_BASIS);
}

TEST_CASE("integers and their quantized float equivalents are distinguishable") {
	// `add_int(1)` and `add_double(1.0)` must not collide: one is a tick counter
	// and the other is a metre, and a hash that conflated them would let a run
	// with the right positions at the wrong times pass.
	StateHash as_int;
	as_int.add_int(1);

	StateHash as_double;
	as_double.add_double(1.0);

	CHECK(as_int.digest() != as_double.digest());
}
