#include "citysim/save/world_io.hpp"

#include "citysim/core/world.hpp"
#include "citysim/save/entity_remap.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace citysim::save {

namespace {

// More than any real game; stops a corrupt count from allocating without bound.
constexpr std::uint64_t k_max_entities = 1ULL << 26;

// Restores the writer's previous remap when the save finishes or throws.
class RemapScope {
  public:
    RemapScope(BinaryWriter& writer, const EntityRemap* remap) : writer_(writer), previous_(writer.remap()) {
        writer_.set_remap(remap);
    }
    ~RemapScope() { writer_.set_remap(previous_); }
    RemapScope(const RemapScope&) = delete;
    RemapScope& operator=(const RemapScope&) = delete;

  private:
    BinaryWriter& writer_;
    const EntityRemap* previous_;
};

// Detaches the reader from a remap that is about to go out of scope.
class ReaderRemapScope {
  public:
    ReaderRemapScope(BinaryReader& reader) : reader_(reader), previous_(reader.remap()) {}
    ~ReaderRemapScope() { reader_.set_remap(previous_); }
    ReaderRemapScope(const ReaderRemapScope&) = delete;
    ReaderRemapScope& operator=(const ReaderRemapScope&) = delete;

  private:
    BinaryReader& reader_;
    const EntityRemap* previous_;
};

void require_serializable(const core::World& world) {
    const auto& registry = world.registry();
    std::string missing;
    for (const core::ComponentInfo& info : registry.entries()) {
        if (!info.serializable()) {
            missing += (missing.empty() ? "" : ", ") + info.name;
        }
    }
    for (const core::SingletonInfo& info : registry.singletons()) {
        if (!info.serializable()) {
            missing += (missing.empty() ? "" : ", ") + info.name;
        }
    }
    if (!missing.empty()) {
        throw std::logic_error("registered types without serialize hooks cannot be saved or loaded: " +
                               missing);
    }
}

} // namespace

void save_world(const core::World& world, BinaryWriter& out) {
    require_serializable(world);

    const std::vector<entt::entity> entities = world.entities();
    const EntityRemap remap = EntityRemap::for_saving(entities);
    const RemapScope scope(out, &remap);

    out.varint(static_cast<std::uint64_t>(entities.size()));

    const auto& registry = world.registry();
    out.varint(static_cast<std::uint64_t>(registry.entries().size()));
    std::vector<core::ComponentRef> refs;
    for (const core::ComponentInfo& info : registry.entries()) {
        refs.clear();
        info.collect(world.entt_registry(), refs);
        std::sort(refs.begin(), refs.end(), [](const core::ComponentRef& a, const core::ComponentRef& b) {
            return entt::to_entity(a.entity) < entt::to_entity(b.entity);
        });

        BinaryWriter payload = out.child();
        payload.varint(static_cast<std::uint64_t>(refs.size()));
        for (const core::ComponentRef& ref : refs) {
            std::uint64_t index = 0;
            remap.index_of(ref.entity, index);
            payload.varint(index);
            info.write(payload, ref.data);
        }
        out.string(info.name);
        out.varint(info.version);
        out.block(payload);
    }

    std::vector<const core::SingletonInfo*> present;
    for (const core::SingletonInfo& info : registry.singletons()) {
        if (info.find(world.ctx()) != nullptr) {
            present.push_back(&info);
        }
    }
    out.varint(static_cast<std::uint64_t>(present.size()));
    for (const core::SingletonInfo* info : present) {
        BinaryWriter payload = out.child();
        info->write(payload, info->find(world.ctx()));
        out.string(info->name);
        out.varint(info->version);
        out.block(payload);
    }
}

WorldLoadReport load_world(core::World& world, BinaryReader& in) {
    require_serializable(world);
    if (world.entity_count() != 0) {
        throw std::logic_error("load_world needs a world with no entities");
    }
    WorldLoadReport report;

    std::uint64_t entity_count = 0;
    in.varint(entity_count);
    if (entity_count > k_max_entities) {
        in.fail("entity count " + std::to_string(entity_count) + " is not plausible");
    }
    EntityRemap remap;
    const ReaderRemapScope remap_scope(in);
    for (std::uint64_t i = 0; i < entity_count; ++i) {
        remap.add(world.create());
    }
    in.set_remap(&remap);

    const auto read_header = [&](std::string& name, std::uint32_t& version) {
        in.string(name);
        std::uint64_t saved_version = 0;
        in.varint(saved_version);
        if (saved_version == 0 || saved_version > 0xFFFFFFFFULL) {
            in.fail("'" + name + "' has an invalid version");
        }
        version = static_cast<std::uint32_t>(saved_version);
    };

    const auto& registry = world.registry();
    std::set<std::string> seen;

    std::uint64_t type_count = 0;
    in.size(type_count);
    for (std::uint64_t t = 0; t < type_count; ++t) {
        std::string name;
        std::uint32_t version = 0;
        read_header(name, version);
        BinaryReader payload = in.block();
        if (!seen.insert("component:" + name).second) {
            in.fail("component type '" + name + "' appears twice");
        }
        const core::ComponentInfo* info = registry.find(name);
        if (info == nullptr) {
            report.skipped_types.push_back(name);
            continue;
        }
        if (version > info->version) {
            in.fail("'" + name + "' was saved with version " + std::to_string(version) +
                    ", newer than this build understands (" + std::to_string(info->version) + ")");
        }
        if (!info->create) {
            in.fail("'" + name + "' cannot be loaded because it is not default-constructible");
        }
        std::uint64_t count = 0;
        payload.size(count);
        std::uint64_t previous_plus_one = 0; // indices must strictly increase, so none repeats
        for (std::uint64_t i = 0; i < count; ++i) {
            std::uint64_t index = 0;
            payload.varint(index);
            if (index < previous_plus_one) {
                payload.fail("component entities of '" + name + "' are not in ascending order");
            }
            previous_plus_one = index + 1;
            entt::entity entity{};
            if (!remap.entity_at(index, entity)) {
                payload.fail("component of '" + name + "' refers to entity " + std::to_string(index) +
                             ", which does not exist");
            }
            void* component = info->create(world.entt_registry(), entity);
            info->read(payload, component, version);
        }
        if (!payload.at_end()) {
            payload.fail("'" + name + "' has " + std::to_string(payload.remaining()) + " unread bytes");
        }
    }

    std::uint64_t singleton_count = 0;
    in.size(singleton_count);
    for (std::uint64_t s = 0; s < singleton_count; ++s) {
        std::string name;
        std::uint32_t version = 0;
        read_header(name, version);
        BinaryReader payload = in.block();
        if (!seen.insert("singleton:" + name).second) {
            in.fail("singleton '" + name + "' appears twice");
        }
        const core::SingletonInfo* info = registry.find_singleton(name);
        if (info == nullptr) {
            report.skipped_types.push_back(name);
            continue;
        }
        if (version > info->version) {
            in.fail("'" + name + "' was saved with version " + std::to_string(version) +
                    ", newer than this build understands (" + std::to_string(info->version) + ")");
        }
        if (!info->get_or_create) {
            in.fail("'" + name + "' cannot be loaded because it is not default-constructible");
        }
        info->read(payload, info->get_or_create(world.ctx()), version);
        if (!payload.at_end()) {
            payload.fail("'" + name + "' has " + std::to_string(payload.remaining()) + " unread bytes");
        }
    }
    return report;
}

} // namespace citysim::save
