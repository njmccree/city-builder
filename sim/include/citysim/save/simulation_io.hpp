#pragma once

#include "citysim/save/csav_file.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace citysim::content {
class ContentDatabase;
}
namespace citysim::core {
class Simulation;
}

namespace citysim::save {

// Saving and loading a whole Simulation as a .csav file (architecture §10). Not tied to a clock: the runner
// calls these between ticks (T0.5.1.4).
//
// Sections written:
//   META  u64 world seed, u64 tick (also in the header, for save lists; the two must agree)
//   WRLD  the World: every entity and registered component and singleton (save_world). The game clock and
//         calendar are registered singletons, so they travel here rather than in separate CLCK/ENTS sections.
//   CMDQ  commands that were waiting for the next tick
//   ILOG  the input log so far (omitted when SaveOptions::include_input_log is false)
// Section versions are 1. Loading skips sections it does not know, with a warning.

struct SaveOptions {
    // Resolves content references (DefHandle) and supplies the mod list and content hash for the header.
    // Optional only for simulations that use no content, such as the headless demo.
    const content::ContentDatabase* content = nullptr;
    bool include_input_log = true;
    // Metadata only; the caller supplies it because simulation code never reads the wall clock.
    std::int64_t saved_at_unix_seconds = 0;
};

struct LoadOptions {
    const content::ContentDatabase* content = nullptr;
};

struct LoadOutcome {
    SaveHeader header;
    // Things the player may want to know; none stops the load. For example: the save was made with
    // different content, a different mod list or another build, or it holds data this build skipped.
    std::vector<std::string> warnings;
};

std::vector<std::byte> save_simulation_bytes(const core::Simulation& simulation, const SaveOptions& options);

// Loads into a Simulation that is fresh (never stepped) and has the same modules installed as the one that
// was saved. Throws SaveFormatError for damaged, truncated or incompatible data; the Simulation is then
// partly loaded and must be discarded. Throws std::logic_error if the Simulation is not fresh.
LoadOutcome load_simulation_bytes(core::Simulation& simulation, std::span<const std::byte> bytes,
                                  const LoadOptions& options);

// File versions. Saving writes to a temporary file next to the target and renames it into place, so a crash
// never leaves a half-written save. Throws std::runtime_error if a file cannot be written or read.
void save_simulation(const core::Simulation& simulation, const std::filesystem::path& path,
                     const SaveOptions& options);
LoadOutcome load_simulation(core::Simulation& simulation, const std::filesystem::path& path,
                            const LoadOptions& options);

// The header of a save file without loading it, for save lists.
SaveHeader read_save_header(const std::filesystem::path& path);

} // namespace citysim::save
