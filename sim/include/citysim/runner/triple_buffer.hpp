#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace citysim::runner {

// Lock-free hand-off of the newest complete value from one producer thread to one consumer thread
// (architecture §7.3). The producer never blocks and never waits for the consumer; the consumer always
// sees the newest value published so far, or an older one if nothing newer exists, never a half-written one.
//
// Three slots rotate between three roles: the producer's `back`, the consumer's `front`, and a `middle`
// slot that is swapped atomically. The dirty bit says the middle slot holds something the consumer has
// not seen.
//
// Exactly ONE thread may call back()/publish() and exactly ONE thread may call read().
template <class T> class TripleBuffer {
  public:
    // The slot the producer fills. Its previous contents are stale: reset every field you care about.
    T& back() { return slots_[back_]; }

    // Hands the filled back slot to the consumer and takes a fresh one.
    void publish() {
        const std::uint32_t previous = middle_.exchange(back_ | k_dirty, std::memory_order_acq_rel);
        back_ = previous & k_index_mask;
    }

    // The newest published value. The reference stays valid, and unchanged, until the next read().
    const T& read() {
        if ((middle_.load(std::memory_order_acquire) & k_dirty) != 0) {
            const std::uint32_t previous = middle_.exchange(front_, std::memory_order_acq_rel);
            front_ = previous & k_index_mask;
        }
        return slots_[front_];
    }

  private:
    static constexpr std::uint32_t k_dirty = 4;
    static constexpr std::uint32_t k_index_mask = 3;

    std::array<T, 3> slots_{};
    std::atomic<std::uint32_t> middle_{1}; // slot 1, clean
    std::uint32_t back_ = 0;               // producer-only
    std::uint32_t front_ = 2;              // consumer-only
};

} // namespace citysim::runner
