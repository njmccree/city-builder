#include "citysim/save/simulation_io.hpp"

#include "citysim/content/content_database.hpp"
#include "citysim/core/commands_serialize.hpp"
#include "citysim/core/log.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/core/version.hpp"
#include "citysim/save/world_io.hpp"

#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace citysim::save {

namespace {

namespace fs = std::filesystem;

constexpr std::uint16_t k_section_version = 1;

std::vector<std::byte> bytes_of(const BinaryWriter& writer) {
    const auto bytes = writer.bytes();
    return {bytes.begin(), bytes.end()};
}

std::vector<ModStamp> mod_stamps(const content::ContentDatabase* content) {
    std::vector<ModStamp> stamps;
    if (content != nullptr) {
        for (const content::ModManifest& mod : content->mods()) {
            stamps.push_back({mod.id, mod.version});
        }
    }
    return stamps;
}

std::string describe_mods(const std::vector<ModStamp>& mods) {
    std::string text;
    for (const ModStamp& mod : mods) {
        text += (text.empty() ? "" : ", ") + mod.id + " " + mod.version;
    }
    return text.empty() ? "none" : text;
}

const RawSection* find_section(const CsavContents& contents, const char* tag) {
    for (const RawSection& section : contents.sections) {
        if (section.tag == tag) {
            return &section;
        }
    }
    return nullptr;
}

const RawSection& require_section(const CsavContents& contents, const char* tag) {
    const RawSection* section = find_section(contents, tag);
    if (section == nullptr) {
        throw SaveFormatError(std::string("the save has no ") + tag + " section", 0);
    }
    return *section;
}

void check_section_version(const RawSection& section) {
    if (section.version == 0 || section.version > k_section_version) {
        throw SaveFormatError("section " + section.tag + " has version " + std::to_string(section.version) +
                                  ", newer than this build understands (" +
                                  std::to_string(k_section_version) + ")",
                              0);
    }
}

// Reads a whole section with `read`, requiring that it uses every byte.
template <class Fn>
void read_section(const RawSection& section, const content::ContentDatabase* content, Fn&& read) {
    check_section_version(section);
    BinaryReader reader(section.data, nullptr, content);
    read(reader);
    if (!reader.at_end()) {
        reader.fail("section " + section.tag + " has " + std::to_string(reader.remaining()) +
                    " unread bytes");
    }
}

} // namespace

std::vector<std::byte> save_simulation_bytes(const core::Simulation& simulation, const SaveOptions& options) {
    CsavContents contents;
    contents.header.game_version = std::string(core::version());
    contents.header.world_seed = simulation.world_seed();
    contents.header.tick = simulation.tick();
    contents.header.content_hash = options.content != nullptr ? options.content->content_hash() : 0;
    contents.header.mods = mod_stamps(options.content);
    contents.header.saved_at_unix_seconds = options.saved_at_unix_seconds;

    const auto add = [&](const char* tag, const BinaryWriter& writer) {
        contents.sections.push_back(RawSection{tag, k_section_version, bytes_of(writer)});
    };

    BinaryWriter meta(nullptr, options.content);
    std::uint64_t seed = simulation.world_seed();
    std::uint64_t tick = simulation.tick();
    io(meta, seed);
    io(meta, tick);
    add("META", meta);

    BinaryWriter world(nullptr, options.content);
    save_world(simulation.world(), world);
    add("WRLD", world);

    BinaryWriter commands(nullptr, options.content);
    std::vector<core::SequencedCommand> pending = simulation.pending_commands();
    io(commands, pending);
    add("CMDQ", commands);

    if (options.include_input_log) {
        BinaryWriter log(nullptr, options.content);
        std::vector<core::InputLogEntry> entries = simulation.input_log().entries();
        io(log, entries);
        add("ILOG", log);
    }
    return write_csav(contents);
}

LoadOutcome load_simulation_bytes(core::Simulation& simulation, std::span<const std::byte> bytes,
                                  const LoadOptions& options) {
    if (simulation.tick() != 0 || !simulation.pending_commands().empty() ||
        simulation.world().entity_count() != 0) {
        throw std::logic_error("a save can only be loaded into a Simulation that has not run");
    }

    CsavContents contents = read_csav(bytes);
    LoadOutcome outcome;
    outcome.header = contents.header;
    const SaveHeader& header = contents.header;

    // Things that differ from the running game are reported, not refused (the UI asks the player).
    if (header.game_version != core::version()) {
        outcome.warnings.push_back("saved with game version " + header.game_version + ", this is " +
                                   std::string(core::version()));
    }
    if (options.content != nullptr) {
        const std::vector<ModStamp> current = mod_stamps(options.content);
        if (header.mods != current) {
            outcome.warnings.push_back("the save was made with different mods (saved: " +
                                       describe_mods(header.mods) + "; now: " + describe_mods(current) + ")");
        }
        if (header.content_hash != options.content->content_hash()) {
            outcome.warnings.push_back("the game content changed since this save was made (content hash "
                                       "differs); things may not behave as they did");
        }
    }

    static const std::set<std::string> k_known{"META", "WRLD", "CMDQ", "ILOG"};
    std::set<std::string> seen;
    for (const RawSection& section : contents.sections) {
        if (!seen.insert(section.tag).second) {
            throw SaveFormatError("section " + section.tag + " appears twice", 0);
        }
        if (!k_known.contains(section.tag)) {
            outcome.warnings.push_back("skipped unknown section " + section.tag);
            CS_LOG_WARN(Sim, "save: skipped unknown section {}", section.tag);
        }
    }

    std::uint64_t seed = 0;
    std::uint64_t tick = 0;
    read_section(require_section(contents, "META"), options.content, [&](BinaryReader& reader) {
        io(reader, seed);
        io(reader, tick);
    });
    if (seed != header.world_seed || tick != header.tick) {
        throw SaveFormatError("the header and the META section disagree about the seed or tick", 0);
    }

    read_section(require_section(contents, "WRLD"), options.content, [&](BinaryReader& reader) {
        const WorldLoadReport report = load_world(simulation.world(), reader);
        for (const std::string& name : report.skipped_types) {
            outcome.warnings.push_back("skipped saved data for '" + name +
                                       "', which this build does not have");
        }
    });

    std::vector<core::SequencedCommand> pending;
    if (const RawSection* section = find_section(contents, "CMDQ")) {
        read_section(*section, options.content, [&](BinaryReader& reader) { io(reader, pending); });
    }
    std::vector<core::InputLogEntry> log;
    if (const RawSection* section = find_section(contents, "ILOG")) {
        read_section(*section, options.content, [&](BinaryReader& reader) { io(reader, log); });
    }

    simulation.restore_loop_state(seed, tick, std::move(pending), std::move(log));
    return outcome;
}

void save_simulation(const core::Simulation& simulation, const fs::path& path, const SaveOptions& options) {
    const std::vector<std::byte> bytes = save_simulation_bytes(simulation, options);
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("cannot write " + temporary.generic_string());
        }
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file) {
            throw std::runtime_error("failed while writing " + temporary.generic_string());
        }
    }
    std::error_code ec;
    fs::rename(temporary, path, ec); // replaces an existing file on both Linux and Windows
    if (ec) {
        fs::remove(temporary, ec);
        throw std::runtime_error("cannot move the save into place at " + path.generic_string());
    }
}

namespace {

std::vector<std::byte> read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path.generic_string());
    }
    const std::vector<char> text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[i] = static_cast<std::byte>(text[i]);
    }
    return bytes;
}

} // namespace

LoadOutcome load_simulation(core::Simulation& simulation, const fs::path& path, const LoadOptions& options) {
    return load_simulation_bytes(simulation, read_file(path), options);
}

SaveHeader read_save_header(const fs::path& path) {
    return read_csav_header(read_file(path));
}

} // namespace citysim::save
