#ifndef KARTGAME_AUDIO_STATE_SEQLOCK_H
#define KARTGAME_AUDIO_STATE_SEQLOCK_H

#include "core/audio_state.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace kartgame {

// One `EngineAudioInput` published from the physics thread and read on the audio
// thread, without a lock and without an allocation. Issue #81's third acceptance
// criterion.
//
// **This exists because there are now three streams that need it** — the engine
// voice, the scrub layer and the wind layer — and three hand-copied seqlocks is
// three places for the memory ordering to drift apart. It is lifted verbatim out
// of `engine_voice.cpp`, which lifted it verbatim out of `audio_probe.cpp`, which
// is where it was measured. Nothing about the algorithm changed in the move.
//
// ## Why not `std::atomic<EngineAudioInput>`
//
// `audio_state.h` says the struct is "publishable by value", which invites exactly
// that, and it must not be used: the struct is 72 bytes,
// `std::atomic<EngineAudioInput>::is_always_lock_free` is **false**, and so
// publishing by value takes a mutex out of libc++'s global table *inside `_mix`*.
// That is precisely what the criterion forbids.
//
// ## The measurements behind it
//
// From ADR-0035 and `audio_probe.cpp`: 3.5 ns to publish, 4.6 ns to read, and
// under torture 1,751,040 reads with **0 torn** while an unsynchronized control
// tore 60,033 times. The retry budget is bounded because an unbounded spin on the
// audio thread is a lock by another name; "gave up" is counted separately from
// "torn" because merging them once reported 1,022 phantom torn reads.
class AudioStateSeqlock {
public:
	// How many times a read may retry before giving up and telling the caller to
	// repeat its last good snapshot. 64 is far more than a 120 Hz writer can force.
	static constexpr int64_t RETRY_BUDGET = 64;

	// Publish one tick. Physics thread. Wait-free for the writer: two relaxed
	// increments of a counter with a release fence between them and the payload, so
	// a slow audio thread can never slow the solver.
	void publish(const kart::core::EngineAudioInput &p_input) {
		// Enter the write: the counter goes odd, and the release ordering means no
		// reader can observe the odd counter *after* observing a byte of the payload
		// written below it.
		const uint32_t start = _seq.load(std::memory_order_relaxed);
		_seq.store(start + 1u, std::memory_order_release);
		std::atomic_thread_fence(std::memory_order_release);

		_payload = p_input;

		// Leave the write. The fence before this store is what makes the payload
		// visible to a reader that then sees an even, unchanged counter.
		std::atomic_thread_fence(std::memory_order_release);
		_seq.store(start + 2u, std::memory_order_release);
	}

	// Read the published state. The audio-thread call. Returns false when the retry
	// budget was exhausted, in which case `r_input` is untouched and the caller is
	// expected to reuse its own last good snapshot — a repeated frame of state is
	// inaudible and a torn one is a parameter no vehicle state ever had.
	//
	// `const` because it is a read of published state and a caller holding a const
	// reference should be able to do it. The atomics are the only mutable things.
	bool read(kart::core::EngineAudioInput &r_input) const {
		int64_t retries = 0;
		for (;;) {
			const uint32_t first = _seq.load(std::memory_order_acquire);
			if ((first & 1u) != 0u) {
				// A write is in progress. Nothing to do but look again.
				++retries;
				if (retries > RETRY_BUDGET) {
					break;
				}
				continue;
			}
			// `memcpy` rather than assignment, deliberately: the source is being written
			// concurrently, so this is a racy read by construction and the validation
			// below is what makes it safe. A struct assignment would be the same machine
			// code with a stronger implication about it.
			std::memcpy(&r_input, &_payload, sizeof(r_input));
			std::atomic_thread_fence(std::memory_order_acquire);
			const uint32_t second = _seq.load(std::memory_order_relaxed);
			if (first == second) {
				_retries.fetch_add(retries, std::memory_order_relaxed);
				return true;
			}
			++retries;
			if (retries > RETRY_BUDGET) {
				break;
			}
		}
		_retries.fetch_add(retries, std::memory_order_relaxed);
		_gave_up.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	int64_t gave_up() const { return _gave_up.load(std::memory_order_relaxed); }
	int64_t retries() const { return _retries.load(std::memory_order_relaxed); }

	// Clears the diagnostics only. **Never the sequence counter**: resetting a
	// transport mid-flight is how a reader gets a payload from before the reset with
	// a sequence number from after it.
	void reset_counters() {
		_gave_up.store(0, std::memory_order_relaxed);
		_retries.store(0, std::memory_order_relaxed);
	}

private:
	// Odd sequence means a write is in progress. A reader that sees an odd count, or
	// a different count either side of the copy, retries.
	mutable std::atomic<uint32_t> _seq{ 0 };
	kart::core::EngineAudioInput _payload{};

	mutable std::atomic<int64_t> _gave_up{ 0 };
	mutable std::atomic<int64_t> _retries{ 0 };
};

} // namespace kartgame

#endif // KARTGAME_AUDIO_STATE_SEQLOCK_H
