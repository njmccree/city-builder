#pragma once

#include "citysim/core/event_bus.hpp"
#include "citysim/core/job_system.hpp"
#include "citysim/core/rng.hpp"
#include "citysim/core/world.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace citysim::core {

// Everything a system may touch during one tick. Built fresh by Simulation::step().
struct TickContext {
    World& world;
    std::uint64_t tick; // the tick being simulated; the same value in every phase of the tick
    rng::RngContext rng;
    JobSystem& jobs;
    EventBus& events;

    // Calls fn(begin, end, chunk_index) over [0, count) in chunks of chunk_size, possibly on several threads.
    // The chunk size is chosen by the caller and never depends on thread count (architecture §7.2), so write
    // only to per-chunk output (ChunkedOutput) and merge it in chunk order afterwards; see JobSystem.
    template <class Fn> void parallel_for(std::size_t count, std::size_t chunk_size, Fn&& fn) {
        jobs.parallel_for(count, chunk_size, std::forward<Fn>(fn));
    }
};

} // namespace citysim::core
