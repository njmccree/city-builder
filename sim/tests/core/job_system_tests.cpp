#include "citysim/core/chunked_output.hpp"
#include "citysim/core/job_system.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/core/state_hasher.hpp"
#include "support/fn_system.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using namespace citysim::core;
using citysim::test::make_system;

namespace {
constexpr std::array<std::size_t, 3> k_worker_counts{1, 2, 8};

std::uint32_t bits_of(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
} // namespace

TEST_CASE("JobSystem resolves its worker count", "[core][jobs]") {
    REQUIRE(JobSystem(1).worker_count() == 1);
    REQUIRE(JobSystem(2).worker_count() == 2);
    REQUIRE(JobSystem(8).worker_count() == 8);
    REQUIRE(JobSystem(0).worker_count() >= 1);
}

TEST_CASE("chunk_count rounds up and treats chunk_size 0 as 1", "[core][jobs]") {
    REQUIRE(JobSystem::chunk_count(0, 4) == 0);
    REQUIRE(JobSystem::chunk_count(1, 4) == 1);
    REQUIRE(JobSystem::chunk_count(8, 4) == 2);
    REQUIRE(JobSystem::chunk_count(9, 4) == 3);
    REQUIRE(JobSystem::chunk_count(5, 0) == 5);
}

TEST_CASE("chunk boundaries do not depend on the worker count", "[core][jobs]") {
    for (const std::size_t workers : k_worker_counts) {
        JobSystem jobs(workers);
        for (const auto& [count, chunk_size] : std::vector<std::pair<std::size_t, std::size_t>>{
                 {0, 5}, {1, 5}, {10, 5}, {11, 5}, {100, 7}, {100, 1}, {100, 1000}, {7, 0}}) {
            std::mutex mutex;
            std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> seen;
            jobs.parallel_for(count, chunk_size, [&](std::size_t begin, std::size_t end, std::size_t chunk) {
                const std::lock_guard lock(mutex);
                seen.emplace_back(chunk, begin, end);
            });
            std::sort(seen.begin(), seen.end());

            const std::size_t size = std::max<std::size_t>(chunk_size, 1);
            REQUIRE(seen.size() == JobSystem::chunk_count(count, chunk_size));
            for (std::size_t i = 0; i < seen.size(); ++i) {
                REQUIRE(std::get<0>(seen[i]) == i);
                REQUIRE(std::get<1>(seen[i]) == i * size);
                REQUIRE(std::get<2>(seen[i]) == std::min(count, (i + 1) * size));
            }
        }
    }
}

TEST_CASE("every index is visited exactly once", "[core][jobs]") {
    for (const std::size_t workers : k_worker_counts) {
        JobSystem jobs(workers);
        std::vector<std::atomic<int>> visits(10007);
        jobs.parallel_for(visits.size(), 64, [&](std::size_t begin, std::size_t end, std::size_t) {
            for (std::size_t i = begin; i < end; ++i) {
                visits[i].fetch_add(1);
            }
        });
        for (const auto& v : visits) {
            REQUIRE(v.load() == 1);
        }
    }
}

TEST_CASE("float sums through parallel_map_reduce are bit-identical for 1, 2 and 8 workers",
          "[core][jobs][determinism]") {
    constexpr std::size_t k_count = 100000;
    const auto sum_with = [&](std::size_t workers, std::size_t chunk_size) {
        JobSystem jobs(workers);
        return jobs.parallel_map_reduce(
            k_count, chunk_size,
            [](std::size_t begin, std::size_t end, std::size_t) {
                float partial = 0.0F;
                for (std::size_t i = begin; i < end; ++i) {
                    partial += std::sin(static_cast<float>(i)) / static_cast<float>(i + 1);
                }
                return partial;
            },
            0.0F, [](float acc, float chunk) { return acc + chunk; });
    };

    for (const std::size_t chunk_size : {std::size_t{37}, std::size_t{1000}, std::size_t{4096}}) {
        const std::uint32_t reference = bits_of(sum_with(1, chunk_size));
        REQUIRE(bits_of(sum_with(2, chunk_size)) == reference);
        REQUIRE(bits_of(sum_with(8, chunk_size)) == reference);
    }
    // Different chunking is a different (valid) computation; that is why chunk size is the caller's choice.
    REQUIRE(sum_with(1, 37) != sum_with(1, 4096));
}

TEST_CASE("parallel_map_reduce folds in chunk order", "[core][jobs]") {
    for (const std::size_t workers : k_worker_counts) {
        JobSystem jobs(workers);
        const std::string joined = jobs.parallel_map_reduce(
            10, 3,
            [](std::size_t begin, std::size_t, std::size_t chunk) {
                return std::to_string(chunk) + ":" + std::to_string(begin) + " ";
            },
            std::string(), [](std::string acc, const std::string& part) { return acc + part; });
        REQUIRE(joined == "0:0 1:3 2:6 3:9 ");
    }
    JobSystem jobs(4);
    REQUIRE(jobs.parallel_map_reduce(
                0, 4, [](std::size_t, std::size_t, std::size_t) { return 1; }, 42,
                [](int acc, int part) { return acc + part; }) == 42);
}

TEST_CASE("ChunkedOutput merges in chunk order and can be reused", "[core][jobs]") {
    for (const std::size_t workers : k_worker_counts) {
        JobSystem jobs(workers);
        ChunkedOutput<std::size_t> out(1000, 64);
        REQUIRE(out.chunk_count() == 16);

        for (int round = 0; round < 2; ++round) {
            jobs.parallel_for(1000, 64, [&](std::size_t begin, std::size_t end, std::size_t chunk) {
                for (std::size_t i = begin; i < end; ++i) {
                    if (i % 3 == 0) {
                        out.chunk(chunk).push_back(i);
                    }
                }
            });
            std::vector<std::size_t> merged;
            out.merge_into(merged);

            std::vector<std::size_t> expected;
            for (std::size_t i = 0; i < 1000; i += 3) {
                expected.push_back(i);
            }
            REQUIRE(merged == expected);
        }
    }
}

TEST_CASE("the exception from the lowest chunk is rethrown, after every chunk ran", "[core][jobs]") {
    for (const std::size_t workers : k_worker_counts) {
        JobSystem jobs(workers);
        for (int attempt = 0; attempt < 20; ++attempt) { // repeat to shake out timing dependence
            std::atomic<std::size_t> ran{0};
            std::string message;
            try {
                jobs.parallel_for(100, 10, [&](std::size_t, std::size_t, std::size_t chunk) {
                    ran.fetch_add(1);
                    if (chunk == 7) {
                        throw std::runtime_error("chunk 7");
                    }
                    if (chunk == 3) {
                        throw std::runtime_error("chunk 3");
                    }
                });
            } catch (const std::runtime_error& error) {
                message = error.what();
            }
            REQUIRE(message == "chunk 3");
            REQUIRE(ran.load() == 10);
        }

        // Still usable afterwards, and a clean run does not rethrow the old error.
        std::atomic<int> sum{0};
        REQUIRE_NOTHROW(
            jobs.parallel_for(10, 1, [&](std::size_t, std::size_t, std::size_t) { sum.fetch_add(1); }));
        REQUIRE(sum.load() == 10);
    }
}

TEST_CASE("an exception from a single chunk propagates too", "[core][jobs]") {
    for (const std::size_t workers : k_worker_counts) {
        JobSystem jobs(workers);
        REQUIRE_THROWS_AS(
            jobs.parallel_for(5, 100,
                              [](std::size_t, std::size_t, std::size_t) { throw std::logic_error("x"); }),
            std::logic_error);
    }
}

TEST_CASE("10,000 tiny parallel_for calls neither deadlock nor lose work", "[core][jobs][stress]") {
    for (const std::size_t workers : k_worker_counts) {
        JobSystem jobs(workers);
        std::atomic<std::size_t> total{0};
        std::size_t expected = 0;
        for (std::size_t call = 0; call < 10000; ++call) {
            const std::size_t count = call % 5; // includes empty and single-chunk calls
            jobs.parallel_for(count, 1, [&](std::size_t begin, std::size_t end, std::size_t) {
                total.fetch_add(end - begin);
            });
            expected += count;
        }
        REQUIRE(total.load() == expected);
    }
}

TEST_CASE("a nested parallel_for runs serially instead of deadlocking", "[core][jobs]") {
    for (const std::size_t workers : k_worker_counts) {
        JobSystem jobs(workers);
        std::atomic<std::size_t> inner_total{0};
        jobs.parallel_for(8, 1, [&](std::size_t, std::size_t, std::size_t) {
            jobs.parallel_for(100, 10, [&](std::size_t begin, std::size_t end, std::size_t) {
                inner_total.fetch_add(end - begin);
            });
        });
        REQUIRE(inner_total.load() == 800);

        // The outer call is unaffected afterwards: a normal call still parallelizes and completes.
        std::atomic<std::size_t> after{0};
        jobs.parallel_for(
            64, 8, [&](std::size_t begin, std::size_t end, std::size_t) { after.fetch_add(end - begin); });
        REQUIRE(after.load() == 64);
    }
}

TEST_CASE("calls from several threads are serialized safely", "[core][jobs][threads]") {
    JobSystem jobs(4);
    std::atomic<std::size_t> total{0};
    std::vector<std::thread> callers;
    for (int t = 0; t < 3; ++t) {
        callers.emplace_back([&] {
            for (int i = 0; i < 200; ++i) {
                jobs.parallel_for(50, 5, [&](std::size_t begin, std::size_t end, std::size_t) {
                    total.fetch_add(end - begin);
                });
            }
        });
    }
    for (auto& caller : callers) {
        caller.join();
    }
    REQUIRE(total.load() == 3u * 200u * 50u);
}

namespace {
struct Cell {
    std::int32_t value = 0;
};
void hash_cell(StateHasher& hasher, const Cell& cell) {
    hasher.add_pod(cell.value);
}

// Data-parallel work inside a system, the way real systems should do it: compute per-chunk results from
// read-only data in parallel, merge in chunk order, then apply serially.
std::vector<std::uint64_t> run_parallel_scenario(std::size_t workers) {
    struct Change {
        World::Entity entity;
        std::int32_t delta;
    };

    Simulation sim(99, workers);
    Hooks<Cell> hooks;
    hooks.hash = &hash_cell;
    sim.world().registry().register_type<Cell>("test.Cell", hooks);

    sim.add_system(Phase::ApplyCommands, make_system("spawn", [](TickContext& context) {
                       if (context.tick == 0) {
                           for (int i = 0; i < 3000; ++i) {
                               context.world.emplace<Cell>(context.world.create(), i % 7);
                           }
                       }
                   }));
    sim.add_system(Phase::Update, make_system("parallel-drift", [](TickContext& context) {
                       std::vector<World::Entity> entities;
                       for (const auto entity : context.world.view<Cell>()) {
                           entities.push_back(entity);
                       }
                       const std::uint64_t stream = citysim::rng::stream("test.parallel-drift");

                       ChunkedOutput<Change> changes(entities.size(), 128);
                       context.parallel_for(
                           entities.size(), 128, [&](std::size_t begin, std::size_t end, std::size_t chunk) {
                               for (std::size_t i = begin; i < end; ++i) {
                                   const auto entity = entities[i];
                                   const std::uint64_t h = context.rng.at(stream, entt::to_integral(entity));
                                   const std::int32_t delta =
                                       static_cast<std::int32_t>(citysim::rng::uniform_int(h, -3, 3));
                                   if (delta != 0) {
                                       changes.chunk(chunk).push_back(
                                           {entity, delta + context.world.get<Cell>(entity).value % 3});
                                   }
                               }
                           });

                       std::vector<Change> merged;
                       changes.merge_into(merged);
                       for (const Change& change : merged) {
                           context.world.get<Cell>(change.entity).value += change.delta;
                       }
                   }));

    std::vector<std::uint64_t> hashes;
    for (int i = 0; i < 30; ++i) {
        sim.step();
        hashes.push_back(sim.world().state_hash());
    }
    return hashes;
}
} // namespace

TEST_CASE("a simulation using parallel_for has identical hashes with 1, 2 and 8 workers",
          "[core][jobs][determinism]") {
    const auto one = run_parallel_scenario(1);
    REQUIRE(run_parallel_scenario(2) == one); // every tick, not just the last
    REQUIRE(run_parallel_scenario(8) == one);
    REQUIRE(one.front() != one.back()); // the scenario really changes state
}
