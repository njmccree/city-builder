#include "headless/bench.hpp"

#include "citysim/core/commands.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/walking_skeleton.hpp"
#include "citysim/time/time_module.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <ostream>

namespace citysim::headless {

TimingSummary summarize(std::vector<double> milliseconds) {
    TimingSummary summary;
    if (milliseconds.empty()) {
        return summary;
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    double sum = 0.0;
    for (const double value : milliseconds) {
        sum += value;
    }
    const auto nearest_rank = [&](double fraction) {
        const auto rank =
            static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(milliseconds.size())));
        return milliseconds[std::clamp<std::size_t>(rank, 1, milliseconds.size()) - 1];
    };
    summary.avg = sum / static_cast<double>(milliseconds.size());
    summary.p50 = nearest_rank(0.50);
    summary.p95 = nearest_rank(0.95);
    summary.max = milliseconds.back();
    return summary;
}

namespace {

void print_summary(std::ostream& out, const char* label, const TimingSummary& summary) {
    out << std::left << std::setw(26) << label << std::right << std::fixed << std::setprecision(3) << "avg "
        << std::setw(9) << summary.avg << "  p50 " << std::setw(9) << summary.p50 << "  p95 " << std::setw(9)
        << summary.p95 << "  max " << std::setw(9) << summary.max << '\n';
}

} // namespace

int run_bench(const BenchSpec& spec, std::uint64_t seed, std::ostream& out) {
    using Clock = std::chrono::steady_clock;

    core::Simulation simulation(seed, spec.threads);
    time::install(simulation);
    debug::WalkingSkeletonConfig config;
    config.map_width = spec.map;
    config.map_height = spec.map;
    config.max_searches_per_tick = spec.searches;
    const debug::WalkingSkeleton skeleton = debug::install(simulation, config);
    simulation.enqueue({core::SequencedCommand{1, 0, debug::SpawnDebugAgents{spec.agents}}});

    out << "bench: agents=" << spec.agents << " ticks=" << spec.ticks << " threads=" << spec.threads
        << " map=" << spec.map << "x" << spec.map << " searches<=" << spec.searches << "/tick seed=" << seed
        << '\n';

    std::vector<double> milliseconds;
    milliseconds.reserve(spec.ticks);
    const Clock::time_point begin = Clock::now();
    for (std::uint32_t tick = 0; tick < spec.ticks; ++tick) {
        const Clock::time_point started = Clock::now();
        simulation.step();
        milliseconds.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
    }
    const double wall_seconds = std::chrono::duration<double>(Clock::now() - begin).count();

    print_summary(out, "ms/tick, all ticks:", summarize(milliseconds));
    const std::vector<double> second_half(
        milliseconds.begin() + static_cast<std::ptrdiff_t>(milliseconds.size() / 2), milliseconds.end());
    print_summary(out, "ms/tick, last half:", summarize(second_half));
    out << std::left << std::setw(26) << "path searches/tick:" << std::right << std::fixed
        << std::setprecision(1) << static_cast<double>(skeleton.planner->searches_total()) / spec.ticks
        << "  (total " << skeleton.planner->searches_total() << ")\n";
    out << std::left << std::setw(26) << "ticks/second:" << std::right << std::fixed << std::setprecision(1)
        << spec.ticks / wall_seconds << "  (wall " << std::setprecision(2) << wall_seconds << " s)\n";
    out << "agents: " << simulation.world().view<debug::DebugWanderer>().size() << ", final hash " << std::hex
        << std::setw(16) << std::setfill('0') << simulation.world().state_hash() << std::dec
        << std::setfill(' ') << '\n';
    return 0;
}

} // namespace citysim::headless
