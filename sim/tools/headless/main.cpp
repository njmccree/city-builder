// citysim_headless: runs the simulation without a front end (architecture §2, §5.3, §13).
//
// This is a tool, not simulation logic, so it may print and read files; the simulation it drives never
// reads a clock. It runs the demo scenario until the base game has real systems.

#include "citysim/core/command_queue.hpp"
#include "citysim/core/simulation.hpp"
#include "headless/cli_options.hpp"
#include "headless/demo_scenario.hpp"
#include "headless/input_log_text.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>

using namespace citysim;

namespace {

void print_hash(std::uint64_t tick, std::uint64_t hash) {
    std::cout << tick << ' ' << std::hex << std::setw(16) << std::setfill('0') << hash << std::dec
              << std::setfill(' ') << '\n';
}

int run(const headless::Options& options) {
    std::uint64_t seed = options.seed;
    std::uint64_t ticks = options.ticks;

    // Replay: the recorded game replaces both the seed and the generated input.
    std::map<std::uint64_t, std::vector<core::SequencedCommand>> recorded;
    if (options.replay) {
        std::ifstream file(*options.replay);
        if (!file) {
            std::cerr << "cannot open " << *options.replay << '\n';
            return 2;
        }
        const headless::LogFile log = headless::read_log_file(file);
        if ((options.seed_given && options.seed != log.seed) ||
            (options.ticks_given && options.ticks != log.ticks)) {
            std::cerr << "--seed/--ticks conflict with the values stored in " << *options.replay << '\n';
            return 2;
        }
        seed = log.seed;
        ticks = log.ticks;
        for (const core::InputLogEntry& entry : log.entries) {
            recorded[entry.tick].push_back(core::SequencedCommand{entry.sequence, entry.tick, entry.cmd});
        }
    }

    core::Simulation sim(seed);
    headless::setup_demo(sim);
    core::CommandQueue queue;
    std::vector<core::SequencedCommand> batch;

    for (std::uint64_t t = 0; t < ticks; ++t) {
        batch.clear();
        if (options.replay) {
            if (const auto it = recorded.find(t); it != recorded.end()) {
                batch = it->second;
            }
        } else {
            for (core::Command& cmd : headless::demo_input(seed, t)) {
                queue.submit(std::move(cmd), t);
            }
            queue.drain(batch);
        }
        sim.enqueue(batch);
        sim.step();

        if (options.hash_every != 0 && sim.tick() % options.hash_every == 0) {
            print_hash(sim.tick(), sim.world().state_hash());
        }
    }

    std::cout << "final " << sim.tick() << ' ' << std::hex << std::setw(16) << std::setfill('0')
              << sim.world().state_hash() << std::dec << std::setfill(' ') << '\n';

    if (options.record_log) {
        std::ofstream file(*options.record_log);
        if (!file) {
            std::cerr << "cannot write " << *options.record_log << '\n';
            return 2;
        }
        headless::write_log_file(file, headless::LogFile{seed, ticks, sim.input_log().entries()});
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    const headless::ParseResult parsed = headless::parse_options(args);
    if (!parsed.error.empty()) {
        std::cerr << "error: " << parsed.error << '\n' << headless::usage();
        return 2;
    }
    if (parsed.options.help) {
        std::cout << headless::usage();
        return 0;
    }
    try {
        return run(parsed.options);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
