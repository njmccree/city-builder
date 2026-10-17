#pragma once

#include "citysim/core/command_queue.hpp"
#include "citysim/core/simulation.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace citysim::test {

// tick -> commands issued just before that tick runs. Commands go through a real CommandQueue, so
// sequence numbers are assigned the way a UI thread's would be.
using Script = std::map<std::uint64_t, std::vector<citysim::core::Command>>;

// Runs `ticks` ticks of a fresh Simulation(seed), after `setup` has registered components, systems and
// handlers. Returns one hash per tick: element i is the state hash after step i, i.e. when
// Simulation::tick() is i + 1.
inline std::vector<std::uint64_t> run_scenario(std::uint64_t seed, std::uint64_t ticks,
                                               const std::function<void(citysim::core::Simulation&)>& setup,
                                               const Script& script = {}) {
    citysim::core::Simulation sim(seed);
    setup(sim);

    citysim::core::CommandQueue queue;
    std::vector<citysim::core::SequencedCommand> batch;
    std::vector<std::uint64_t> hashes;
    hashes.reserve(static_cast<std::size_t>(ticks));

    for (std::uint64_t tick = 0; tick < ticks; ++tick) {
        if (const auto it = script.find(tick); it != script.end()) {
            for (const citysim::core::Command& cmd : it->second) {
                queue.submit(cmd, tick);
            }
        }
        batch.clear();
        queue.drain(batch);
        sim.enqueue(batch);
        sim.step();
        hashes.push_back(sim.world().state_hash());
    }
    return hashes;
}

// The index of the first tick at which the two runs differ (a shorter run counts as differing where it
// ends), or nullopt if they are identical.
inline std::optional<std::size_t> first_divergence(const std::vector<std::uint64_t>& a,
                                                   const std::vector<std::uint64_t>& b) {
    const std::size_t common = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (a[i] != b[i]) {
            return i;
        }
    }
    if (a.size() != b.size()) {
        return common;
    }
    return std::nullopt;
}

// Fails the test, naming the first diverging tick, unless both runs match at every tick.
inline void expect_same_hashes(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b) {
    const auto diverged = first_divergence(a, b);
    INFO("first diverging tick: " << (diverged ? static_cast<long long>(*diverged) : -1LL)
                                  << " (hash after step; lengths " << a.size() << " and " << b.size() << ")");
    REQUIRE_FALSE(diverged.has_value());
}

} // namespace citysim::test
