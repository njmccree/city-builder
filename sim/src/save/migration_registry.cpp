#include "citysim/save/migration_registry.hpp"

#include <stdexcept>

namespace citysim::save {

namespace {

std::string missing_step(const std::string& what, const std::string& name, std::uint32_t from,
                         std::uint32_t saved, std::uint32_t current) {
    return "cannot load " + what + " '" + name + "': it was saved at version " + std::to_string(saved) +
           " and this build is at version " + std::to_string(current) + ", but there is no migration from " +
           std::to_string(from) + " to " + std::to_string(from + 1);
}

} // namespace

void MigrationRegistry::add_type(const std::string& stable_name, std::uint32_t from_version, MigrationFn fn) {
    if (!types_.emplace(std::make_pair(stable_name, from_version), std::move(fn)).second) {
        throw std::logic_error("migration already registered for '" + stable_name + "' from version " +
                               std::to_string(from_version));
    }
}

void MigrationRegistry::add_section(const std::string& tag, std::uint32_t from_version, MigrationFn fn) {
    if (!sections_.emplace(std::make_pair(tag, from_version), std::move(fn)).second) {
        throw std::logic_error("migration already registered for section " + tag + " from version " +
                               std::to_string(from_version));
    }
}

const MigrationFn* MigrationRegistry::find_type(const std::string& stable_name,
                                                std::uint32_t from_version) const {
    const auto it = types_.find({stable_name, from_version});
    return it == types_.end() ? nullptr : &it->second;
}

const MigrationFn* MigrationRegistry::find_section(const std::string& tag, std::uint32_t from_version) const {
    const auto it = sections_.find({tag, from_version});
    return it == sections_.end() ? nullptr : &it->second;
}

void read_migrated_instance(const MigrationRegistry* registry, const std::string& name,
                            std::uint32_t saved_version, std::uint32_t current_version, BinaryReader& in,
                            const std::function<void(BinaryReader&)>& read_current) {
    if (saved_version == current_version) {
        read_current(in);
        return;
    }
    // saved_version > current_version is rejected by the caller; treat it as a bug here.
    if (saved_version > current_version) {
        throw std::logic_error("read_migrated_instance: saved version is newer than the current one");
    }

    // The first step reads straight from the stream, so it decides where the instance ends; later steps work
    // on the previous step's output.
    BinaryReader* source = &in;
    std::vector<std::byte> buffer;
    BinaryReader staged(std::span<const std::byte>{}, in.remap(), in.content());
    for (std::uint32_t version = saved_version; version < current_version; ++version) {
        const MigrationFn* step = registry != nullptr ? registry->find_type(name, version) : nullptr;
        if (step == nullptr) {
            in.fail(missing_step("data of type", name, version, saved_version, current_version));
        }
        BinaryWriter out;
        (*step)(*source, out);
        if (source != &in && !source->at_end()) {
            in.fail("the migration of '" + name + "' from version " + std::to_string(version) + " left " +
                    std::to_string(source->remaining()) + " bytes unread");
        }
        buffer = out.take();
        staged = BinaryReader(buffer, in.remap(), in.content(), in.offset());
        source = &staged;
    }
    read_current(*source);
    if (!source->at_end()) {
        in.fail("the migrated data of '" + name + "' has " + std::to_string(source->remaining()) +
                " bytes the current layout did not read");
    }
}

std::vector<std::byte> migrate_section(const MigrationRegistry* registry, const std::string& tag,
                                       std::uint32_t saved_version, std::uint32_t current_version,
                                       std::span<const std::byte> data) {
    std::vector<std::byte> bytes(data.begin(), data.end());
    for (std::uint32_t version = saved_version; version < current_version; ++version) {
        const MigrationFn* step = registry != nullptr ? registry->find_section(tag, version) : nullptr;
        if (step == nullptr) {
            throw SaveFormatError(missing_step("section", tag, version, saved_version, current_version), 0);
        }
        BinaryReader in(bytes);
        BinaryWriter out;
        (*step)(in, out);
        if (!in.at_end()) {
            in.fail("the migration of section " + tag + " from version " + std::to_string(version) +
                    " left " + std::to_string(in.remaining()) + " bytes unread");
        }
        bytes = out.take();
    }
    return bytes;
}

} // namespace citysim::save
