#include "citysim/world/world_module.hpp"

#include "citysim/core/log.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/save/bind_serializer.hpp"
#include "citysim/save/save_format_error.hpp"
#include "citysim/world/city_module.hpp"
#include "citysim/world/flood_module.hpp"
#include "citysim/world/resource_fields.hpp"
#include "citysim/world/resource_module.hpp"
#include "citysim/world/terraform_module.hpp"
#include "citysim/world/terrain_serialize.hpp"

#include <stdexcept>
#include <utility>

namespace citysim::world {

namespace {

void hash_water(core::StateHasher& hasher, const WaterState& water) {
    hasher.add_pod(static_cast<std::uint64_t>(water.reach_count()));
    for (const float offset : water.level_offsets()) {
        hasher.add_float(offset);
    }
}

void hash_terrain(core::StateHasher& hasher, const WorldTerrain& terrain) {
    // Exactly SavedTerrain: the heights follow from it.
    hasher.add(terrain.map_id());
    hasher.add_pod(terrain.package_hash());
    hasher.add_pod(terrain.generator_version());
    const auto& tiles = terrain.tiles();
    hasher.add_pod(static_cast<std::uint64_t>(tiles.size()));
    for (const auto& entry : tiles) { // a std::map: sorted by TileCoord
        hasher.add_pod(entry.first.tx);
        hasher.add_pod(entry.first.ty);
    }
    terrain.deltas().hash(hasher);
}

std::string tile_text(TileCoord tile) {
    return "(" + std::to_string(tile.tx) + ", " + std::to_string(tile.ty) + ")";
}

void restore_after_load(core::Simulation& simulation, const content::ContentDatabase& content,
                        std::vector<std::string>& warnings) {
    // Changes to resources this content does not have were dropped on load.
    if (auto* resources = simulation.world().ctx().find<ResourceFields>(); resources != nullptr) {
        for (std::string& warning : resources->take_load_warnings()) {
            CS_LOG_WARN(Save, "load: {}", warning);
            warnings.push_back(std::move(warning));
        }
    }
    auto* terrain = simulation.world().ctx().find<WorldTerrain>();
    if (terrain == nullptr) {
        return;
    }
    const std::optional<SavedTerrain> saved = terrain->take_staged_restore();
    if (!saved) {
        return; // the save had no terrain: keep the installed map with no active tiles
    }
    for (std::string& warning : restore_terrain(*terrain, *saved, content, simulation.jobs())) {
        CS_LOG_WARN(Save, "load: {}", warning);
        warnings.push_back(std::move(warning));
    }
    // The river levels belong to the river layout of the map: another layout (a rebuilt package) cannot use
    // them.
    if (auto* water = simulation.world().ctx().find<WaterState>(); water != nullptr) {
        const std::size_t reaches = terrain->map()->rivers().reach_count();
        if (water->reach_count() != reaches) {
            std::string warning = "the rivers of map '" + terrain->map_id() +
                                  "' changed since this save was made (" +
                                  std::to_string(water->reach_count()) + " reaches, now " +
                                  std::to_string(reaches) + "); the water levels were reset";
            CS_LOG_WARN(Save, "load: {}", warning);
            warnings.push_back(std::move(warning));
            water->reset(reaches);
        }
    }
}

} // namespace

const content::MapDef& find_map_def(const content::ContentDatabase& content, std::string_view map_id) {
    const auto& maps = content.registry<content::MapDef>();
    const auto handle = maps.find(map_id);
    if (!handle) {
        throw std::invalid_argument("unknown map '" + std::string(map_id) + "'");
    }
    return maps.get(*handle);
}

std::shared_ptr<const LoadedMap> open_map(const content::ContentDatabase& content, std::string_view map_id) {
    const auto& maps = content.registry<content::MapDef>();
    const auto handle = maps.find(map_id);
    if (!handle) {
        throw std::invalid_argument("unknown map '" + std::string(map_id) + "'");
    }
    const content::MapDef& def = maps.get(*handle);
    return LoadedMap::open(content::map_package_dir(content.origin(*handle), def), def.id.str());
}

void install(core::Simulation& simulation, const content::ContentDatabase& content,
             const WorldConfig& config) {
    if (config.map_id.empty()) {
        throw std::invalid_argument("world::install needs a map id");
    }
    const content::MapDef& def = find_map_def(content, config.map_id);
    std::shared_ptr<const LoadedMap> map = config.map ? config.map : open_map(content, config.map_id);
    if (map->map_id() != def.id.str()) {
        throw std::invalid_argument("world::install: the map given is '" + map->map_id() + "', not '" +
                                    def.id.str() + "'");
    }

    core::World& world = simulation.world();
    core::Hooks<WorldTerrain> hooks;
    hooks.hash = &hash_terrain;
    save::bind_serializer(hooks);
    world.registry().register_singleton<WorldTerrain>("world.Terrain", hooks);
    world.ctx().emplace<WorldTerrain>(std::move(map), def.map_seed, config.terrain);
    core::Hooks<WaterState> water_hooks;
    water_hooks.hash = &hash_water;
    save::bind_serializer(water_hooks);
    world.registry().register_singleton<WaterState>("world.Water", water_hooks);
    world.ctx().emplace<WaterState>(world.ctx().get<WorldTerrain>().map()->rivers().reach_count());
    install_cities(simulation, config.cities);
    install_flood(simulation);
    install_terraform(simulation);
    install_resources(simulation, content);

    const content::ContentDatabase* database = &content;
    simulation.add_after_load_hook([database](core::Simulation& loaded, std::vector<std::string>& warnings) {
        restore_after_load(loaded, *database, warnings);
    });
}

std::vector<std::string> restore_terrain(WorldTerrain& terrain, const SavedTerrain& saved,
                                         const content::ContentDatabase& content, core::JobSystem& jobs) {
    std::vector<std::string> warnings;
    terrain.deactivate_all();
    terrain.restore_deltas({});
    if (!terrain.has_map() || saved.map_id != terrain.map_id()) {
        const auto& maps = content.registry<content::MapDef>();
        if (!maps.find(saved.map_id)) {
            throw save::SaveFormatError(
                "the save is on map '" + saved.map_id + "', which the content does not have", 0);
        }
        terrain = WorldTerrain(open_map(content, saved.map_id), find_map_def(content, saved.map_id).map_seed,
                               terrain.terrain_defines());
    }
    if (saved.package_hash != terrain.package_hash()) {
        warnings.push_back("the terrain package of map '" + saved.map_id +
                           "' changed since this save was made (package hash " +
                           hash_to_hex(saved.package_hash) + ", now " + hash_to_hex(terrain.package_hash()) +
                           "); the terrain may differ");
    }
    if (saved.generator_version != terrain.generator_version()) {
        warnings.push_back("the terrain generator changed since this save was made (version " +
                           std::to_string(saved.generator_version) + ", now " +
                           std::to_string(terrain.generator_version()) + "); the terrain may differ");
    }
    // The edits first, so that activating a tile adds its edits to the synthesized heights. Layers of tiles
    // that are not on the map (a smaller map under the same id) are dropped with a warning.
    std::vector<SavedTileDelta> deltas;
    for (const SavedTileDelta& delta : saved.deltas) {
        if (terrain.map()->contains_tile(delta.coord)) {
            deltas.push_back(delta);
        } else {
            warnings.push_back("skipped the terrain edits of tile " + tile_text(delta.coord) +
                               ", which is not on the map");
        }
    }
    terrain.restore_deltas(std::move(deltas));
    std::vector<TileCoord> tiles;
    tiles.reserve(saved.active_tiles.size());
    for (const TileCoord tile : saved.active_tiles) {
        if (terrain.map()->contains_tile(tile)) {
            tiles.push_back(tile);
        } else {
            warnings.push_back("skipped terrain tile " + tile_text(tile) + ", which is not on the map");
        }
    }
    terrain.activate_tiles(jobs, tiles);
    return warnings;
}

} // namespace citysim::world
