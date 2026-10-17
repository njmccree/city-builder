#pragma once

#include "citysim/core/event_bus.hpp"
#include "citysim/core/rng.hpp"
#include "citysim/core/world.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace citysim::core {

class JobSystem; // arrives with the multithreading story (S0.2.4)

// Everything a system may touch during one tick. Built fresh by Simulation::step().
struct TickContext {
    World& world;
    std::uint64_t tick; // the tick being simulated; the same value in every phase of the tick
    rng::RngContext rng;
    JobSystem* jobs; // null until a JobSystem exists; use parallel_for() so null is always handled
    EventBus& events;

    // Calls fn(begin, end, chunk_index) over [0, count) in chunks of chunk_size. The chunk size is chosen by
    // the caller and never depends on thread count (architecture §7.2). With no JobSystem the chunks run
    // serially in chunk order; once one exists, chunks may run in parallel, so fn must write only to
    // per-chunk output that the caller merges in chunk order afterwards.
    template <class Fn> void parallel_for(std::size_t count, std::size_t chunk_size, Fn&& fn) {
        const std::size_t size = std::max<std::size_t>(chunk_size, 1);
        std::size_t chunk_index = 0;
        for (std::size_t begin = 0; begin < count; begin += size, ++chunk_index) {
            fn(begin, std::min(begin + size, count), chunk_index);
        }
    }
};

} // namespace citysim::core
