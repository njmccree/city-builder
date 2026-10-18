#include "headless/bench.hpp"

#include "citysim/content/content_database.hpp"
#include "citysim/core/commands.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/walking_skeleton.hpp"
#include "citysim/time/time_module.hpp"
#include "citysim/world/world_module.hpp"
#include "mapgen/synthetic_map.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
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

// A temporary content folder with one mod ("test") that holds the synthetic world, removed when it goes out
// of scope.
class TempWorld {
  public:
    explicit TempWorld(std::uint64_t seed) {
        root_ = std::filesystem::temp_directory_path() / ("citysim_bench_world_" + std::to_string(seed));
        std::filesystem::remove_all(root_);
        const std::filesystem::path package = root_ / "maps" / mapgen::k_synthetic_package_folder;
        mapgen::write_synthetic_package(mapgen::SyntheticSpec{seed, 16.0}, package);
        write(root_ / "mod.json", R"({ "id": "test", "name_key": "mod.test.name", "version": "1.0.0",
  "depends_on": ["base"], "enabled_by_default": false })");
        std::ifstream definition(package / "map_def.json", std::ios::binary);
        write(root_ / "defs" / "maps.json", std::string(std::istreambuf_iterator<char>(definition), {}));
        write(root_ / "loc" / "en" / "maps.json", R"({ "mod.test.name": "Test",
  "map.synthetic.name": "Synthetic", "map.synthetic.desc": "A synthetic test map." })");
    }
    TempWorld(const TempWorld&) = delete;
    TempWorld& operator=(const TempWorld&) = delete;
    ~TempWorld() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    const std::filesystem::path& mod_dir() const { return root_; }

  private:
    static void write(const std::filesystem::path& path, const std::string& text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    std::filesystem::path root_;
};

} // namespace

int run_bench(const BenchSpec& spec, std::uint64_t seed, std::ostream& out) {
    using Clock = std::chrono::steady_clock;

    // With map=world the world game (a synthetic map and one city) is set up before the timing starts; the
    // agents are spawned in the first timed tick, after the city's walk grid exists.
    double activation_ms = 0.0;
    std::unique_ptr<TempWorld> world_files;
    std::unique_ptr<content::ContentDatabase> database;
    core::Simulation simulation(seed, spec.threads);
    time::install(simulation);
    debug::WalkingSkeletonConfig config;
    config.map_width = spec.map;
    config.map_height = spec.map;
    config.max_searches_per_tick = spec.searches;
    if (spec.world) {
        world_files = std::make_unique<TempWorld>(seed);
        database = std::make_unique<content::ContentDatabase>();
        if (!database->load({world_files->mod_dir()}).ok()) {
            throw std::runtime_error("the bench world's content did not load");
        }
        world::WorldConfig world_config;
        world_config.map_id = mapgen::k_synthetic_map_id;
        world::install(simulation, *database, world_config);
        config.source = debug::WalkGridSource::Terrain;
    }
    const debug::WalkingSkeleton skeleton = debug::install(simulation, config);
    if (spec.world) {
        // The detail tiles of the 16 km map are (2..5, 3..6): one city fits exactly there.
        simulation.enqueue({core::SequencedCommand{1, 0, world::FoundCity{2, 3, "Bench"}}});
        const Clock::time_point activation_started = Clock::now();
        simulation.step(); // activates the city's tiles; the walk grid is built in the same tick
        activation_ms = std::chrono::duration<double, std::milli>(Clock::now() - activation_started).count();
        simulation.enqueue({core::SequencedCommand{2, 1, debug::SpawnDebugAgents{spec.agents}}});
    } else {
        simulation.enqueue({core::SequencedCommand{1, 0, debug::SpawnDebugAgents{spec.agents}}});
    }

    out << "bench: agents=" << spec.agents << " ticks=" << spec.ticks << " threads=" << spec.threads;
    if (spec.world) {
        out << " map=world (2048x2048 cells of terrain)";
    } else {
        out << " map=" << spec.map << "x" << spec.map;
    }
    out << " searches<=" << spec.searches << "/tick seed=" << seed << '\n';
    if (spec.world) {
        out << std::left << std::setw(26) << "city activation tick:" << std::right << std::fixed
            << std::setprecision(1) << activation_ms << " ms (36 terrain tiles and the walk grid)\n";
    }

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
