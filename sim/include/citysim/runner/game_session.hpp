#pragma once

#include "citysim/content/content_database.hpp"
#include "citysim/runner/clock.hpp"
#include "citysim/runner/simulation_runner.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace citysim::runner {

struct NewGameRequest {
    std::uint64_t seed = 1;
    // Folders searched for mods (see content::discover_mods), and which mods to load; empty = those enabled
    // by default. The base game (mod "base") is required.
    std::vector<std::filesystem::path> content_roots;
    std::vector<std::string> enabled_mods;
    std::string era_id; // the era the game starts in, e.g. "base:era.ancient"
    // The map (a MapDef id, e.g. "base:map.ancient_near_east"). Not empty installs the world module
    // (world::install: the terrain service, with no city tiles active yet) and the "terrain.tiles" publish
    // hook; the package is opened once, here, and shared by every Simulation of the session. Empty leaves the
    // game without a world.
    std::string map_id;
    // Where the runner's autosaves go; empty turns them off.
    std::filesystem::path autosave_directory;
    // Not owned. Null uses the real clock.
    IClock* clock = nullptr;
    std::size_t worker_count = 0; // see Simulation; 0 = hardware default
    // Above 0 installs the walking skeleton (debug agents on a square test map this many cells wide, 4 m per
    // cell) and the publish hooks for its snapshot data. 0 leaves the game without it.
    std::uint32_t test_map_size = 0;
    // Called on the runner thread after every tick, in this order, to add data to the snapshot (channels).
    std::vector<SimulationRunner::PublishHook> publish_hooks;
};

// Everything a running game needs, assembled and owned in one place: the loaded content, the clock, and the
// SimulationRunner with the time module and calendar installed (so a load recreates them too). This is what
// the GDExtension's CitySim node holds; it has no Godot dependency so it can be tested natively. Not tied to
// a clock itself.
struct NewGameOutcome;
NewGameOutcome start_new_game(const NewGameRequest& request);

class GameSession {
  public:
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    ~GameSession(); // stops the runner first

    SimulationRunner& runner() { return *runner_; }
    const content::ContentDatabase& content() const { return *content_; }
    // The runner's clock now, in the seconds that RenderSnapshot::published_at uses.
    double now_seconds() { return clock_->now_seconds(); }
    // Hands out a snapshot; one consumer thread only (see SimulationRunner::latest_snapshot).
    RenderSnapshot latest_snapshot() { return runner_->latest_snapshot(); }

  private:
    GameSession() = default;
    friend NewGameOutcome start_new_game(const NewGameRequest& request);

    std::unique_ptr<content::ContentDatabase> content_;
    std::unique_ptr<IClock> owned_clock_;
    IClock* clock_ = nullptr;
    // Declared last so it is destroyed first: the runner thread uses everything above.
    std::unique_ptr<SimulationRunner> runner_;
};

struct NewGameOutcome {
    std::unique_ptr<GameSession> session; // null if the game could not start
    std::vector<content::ContentIssue> errors;
    std::vector<content::ContentIssue> warnings;
    bool ok() const { return session != nullptr; }
};

// Loads the content, creates a SimulationRunner, installs the time module and calendar, and starts it. Never
// throws for bad input: problems come back as errors (content problems with file and pointer, other problems
// with an empty file).
NewGameOutcome start_new_game(const NewGameRequest& request);

} // namespace citysim::runner
