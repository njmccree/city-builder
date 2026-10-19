#include "citysim/world/resource_module.hpp"

#include "citysim/content/resource_def.hpp"
#include "citysim/core/isystem.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/core/tick_context.hpp"
#include "citysim/profiling/profile.hpp"
#include "citysim/save/bind_serializer.hpp"
#include "citysim/world/resource_fields.hpp"
#include "citysim/world/resource_serialize.hpp"
#include "citysim/world/world_terrain.hpp"

#include <algorithm>
#include <map>

namespace citysim::world {

namespace {

// The environmental fields every map package may have (architecture §15.8), whatever the content says.
constexpr const char* k_builtin_layers[] = {"fertility", "vegetation", "wetness"};

class ResourcePrepareSystem final : public core::ISystem {
  public:
    std::string_view name() const override { return "world.resource_prepare"; }

    void update(core::TickContext& context) override {
        CS_PROFILE_ZONE("world.resource_prepare_system");
        auto* fields = context.world.ctx().find<ResourceFields>();
        const auto* terrain = context.world.ctx().find<WorldTerrain>();
        if (fields == nullptr || terrain == nullptr) {
            return;
        }
        std::vector<TileCoord> tiles = terrain->active_tiles();
        if (tiles == last_tiles_) {
            return;
        }
        fields->prepare(context.jobs, tiles);
        last_tiles_ = std::move(tiles);
    }

  private:
    std::vector<TileCoord> last_tiles_; // a cache of what was prepared, not game state
};

void hash_fields(core::StateHasher& hasher, const ResourceFields& fields) {
    fields.hash(hasher);
}

} // namespace

void install_resources(core::Simulation& simulation, const content::ContentDatabase& content) {
    core::World& world = simulation.world();
    const auto& terrain = world.ctx().get<WorldTerrain>();

    // layer name -> the ids of the resources that use it
    std::map<std::string, std::vector<std::string>> by_layer;
    for (const char* layer : k_builtin_layers) {
        by_layer[layer];
    }
    const auto& defs = content.registry<content::ResourceDef>();
    for (std::uint32_t i = 0; i < defs.size(); ++i) {
        const content::DefHandle<content::ResourceDef> handle{i};
        by_layer[defs.get(handle).layer].push_back(defs.id(handle).str());
    }
    std::vector<FieldInfo> fields;
    for (auto& [layer, ids] : by_layer) { // sorted by layer name
        std::sort(ids.begin(), ids.end());
        fields.push_back(FieldInfo{layer, std::move(ids)});
    }

    core::Hooks<ResourceFields> hooks;
    hooks.hash = &hash_fields;
    save::bind_serializer(hooks);
    world.registry().register_singleton<ResourceFields>("world.Resources", hooks);
    world.ctx().emplace<ResourceFields>(terrain.map(), terrain.map_seed(), std::move(fields));
    simulation.add_system(core::Phase::PreUpdate, std::make_unique<ResourcePrepareSystem>());
}

} // namespace citysim::world
