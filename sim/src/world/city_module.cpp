#include "citysim/world/city_module.hpp"

#include "citysim/core/log.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/core/ui_notice.hpp"
#include "citysim/profiling/profile.hpp"
#include "citysim/save/bind_serializer.hpp"
#include "citysim/time/calendar_state.hpp"
#include "citysim/world/city_commands.hpp"
#include "citysim/world/city_serialize.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace citysim::world {

namespace {

void hash_defines(core::StateHasher& hasher, const CityDefines& defines) {
    hasher.add_pod(defines.max_player_cities);
    hasher.add_float(defines.min_land_fraction);
    hasher.add_pod(defines.active_ring_tiles);
}

void hash_registry(core::StateHasher& hasher, const CityRegistry& registry) {
    hasher.add_pod(registry.next_id());
    hasher.add_pod(static_cast<std::uint64_t>(registry.size()));
    for (const CityInfo& city : registry.cities()) {
        hasher.add_pod(city.id);
        hasher.add(city.name);
        hasher.add_pod(city.first_tile.tx);
        hasher.add_pod(city.first_tile.ty);
        hasher.add_pod(city.founded_on.year);
        hasher.add_pod(city.founded_on.month);
        hasher.add_pod(city.founded_on.day);
        hasher.add_pod(static_cast<std::uint8_t>(city.mode));
    }
}

void hash_active(core::StateHasher& hasher, const ActiveCity& active) {
    hasher.add_pod(active.city_id);
}

// Fraction of the region cells whose center lies inside the area that are land. A cell is land if the
// "land_mask" layer says so (above 0.5), or, for a package without that layer, if its elevation is above sea
// level.
double land_fraction(const LoadedMap& map, const CityArea& area) {
    const Raster<float>* mask = map.region_layer("land_mask");
    const Raster<float>& layer = mask != nullptr ? *mask : map.region_elevation();
    const double threshold = mask != nullptr ? 0.5 : 0.0;
    const double cell = map.manifest().region.cell_size_m;
    const WorldPos origin = area.origin();
    // Cell i has its center at (i + 0.5) * cell: it is inside [lo, hi) for i from ceil(lo / cell - 0.5).
    const auto first_cell = [&](double lo) { return static_cast<std::int64_t>(std::ceil(lo / cell - 0.5)); };
    const std::int64_t x0 = first_cell(origin.x);
    const std::int64_t x1 = first_cell(origin.x + k_city_area_size_m);
    const std::int64_t z0 = first_cell(origin.z);
    const std::int64_t z1 = first_cell(origin.z + k_city_area_size_m);
    std::int64_t total = 0;
    std::int64_t land = 0;
    for (std::int64_t z = z0; z < z1; ++z) {
        for (std::int64_t x = x0; x < x1; ++x) {
            if (!layer.in_bounds(x, z)) {
                continue;
            }
            ++total;
            const float value = layer.value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z));
            land += (value == value && static_cast<double>(value) > threshold) ? 1 : 0;
        }
    }
    return total == 0 ? 0.0 : static_cast<double>(land) / static_cast<double>(total);
}

time::Date today(const core::World& world) {
    const auto* calendar = world.ctx().find<time::CalendarState>();
    return calendar != nullptr ? time::current_date(*calendar) : time::Date{};
}

void notify(core::World& world, const char* tag, const char* key, std::uint32_t id) {
    world.ctx().get<core::UiOutbox>().push(core::UiNotice{tag, key, id});
}

// Makes `city_id` the active city: its tiles and ring become active terrain, the previous city's tiles that
// the new set does not need are dropped, and the sim modes follow.
void activate_city(core::TickContext& context, std::uint32_t city_id) {
    CS_PROFILE_ZONE("world.activate_city");
    core::World& world = context.world;
    auto& registry = world.ctx().get<CityRegistry>();
    auto& active = world.ctx().get<ActiveCity>();
    auto& terrain = world.ctx().get<WorldTerrain>();
    const std::uint32_t ring = world.ctx().get<CityDefines>().active_ring_tiles;
    const CityInfo* city = registry.find(city_id);

    const std::vector<TileCoord> wanted = city_active_tiles(*terrain.map(), city->area(), ring);
    if (const CityInfo* previous = registry.find(active.city_id); previous != nullptr) {
        std::vector<TileCoord> drop;
        for (const TileCoord tile : city_active_tiles(*terrain.map(), previous->area(), ring)) {
            if (!std::binary_search(wanted.begin(), wanted.end(), tile)) {
                drop.push_back(tile);
            }
        }
        terrain.deactivate_tiles(drop);
        registry.find(previous->id)->mode = SimMode::Aggregate;
    }
    terrain.activate_tiles(context.jobs, wanted);
    registry.find(city_id)->mode = SimMode::Agent;
    active.city_id = city_id;
}

void found_city(core::TickContext& context, const FoundCity& command) {
    core::World& world = context.world;
    auto& registry = world.ctx().get<CityRegistry>();
    const auto& terrain = world.ctx().get<WorldTerrain>();
    const CityDefines& defines = world.ctx().get<CityDefines>();
    const TileCoord origin{command.origin_tx, command.origin_ty};

    const std::string_view problem =
        found_city_problem(terrain, registry, defines, origin, command.name.size());
    if (!problem.empty()) {
        CS_LOG_INFO(Sim, "FoundCity at ({}, {}) refused: {}", origin.tx, origin.ty, problem);
        notify(world, "city_found_failed", std::string(problem).c_str(), 0);
        return;
    }
    const std::uint32_t id = registry.next_id();
    std::string name = command.name.empty() ? "City " + std::to_string(id) : command.name;
    registry.add(std::move(name), origin, today(world), SimMode::Aggregate);
    notify(world, "city_founded", "", id);
    if (world.ctx().get<ActiveCity>().city_id == 0) {
        activate_city(context, id); // the first city is the one being viewed
        notify(world, "active_city_changed", "", id);
    }
}

void set_active_city(core::TickContext& context, const SetActiveCity& command) {
    core::World& world = context.world;
    if (world.ctx().get<CityRegistry>().find(command.city_id) == nullptr) {
        CS_LOG_INFO(Sim, "SetActiveCity {} refused: no such city", command.city_id);
        notify(world, "active_city_failed", k_error_unknown_city, command.city_id);
        return;
    }
    if (world.ctx().get<ActiveCity>().city_id == command.city_id) {
        return;
    }
    activate_city(context, command.city_id);
    notify(world, "active_city_changed", "", command.city_id);
}

} // namespace

void validate(const CityDefines& defines) {
    if (defines.max_player_cities == 0) {
        throw std::invalid_argument("CityDefines: max_player_cities must be positive");
    }
    if (!(defines.min_land_fraction >= 0.0F && defines.min_land_fraction <= 1.0F)) {
        throw std::invalid_argument("CityDefines: min_land_fraction must be between 0 and 1");
    }
    if (defines.active_ring_tiles > 8) {
        throw std::invalid_argument("CityDefines: active_ring_tiles must be at most 8");
    }
}

std::vector<TileCoord> city_active_tiles(const LoadedMap& map, const CityArea& area, std::uint32_t ring) {
    std::vector<TileCoord> tiles;
    for (const TileCoord tile : area.tiles(static_cast<std::int32_t>(ring))) {
        if (map.contains_tile(tile)) {
            tiles.push_back(tile);
        }
    }
    return tiles; // CityArea::tiles() is sorted
}

std::string_view found_city_problem(const WorldTerrain& terrain, const CityRegistry& registry,
                                    const CityDefines& defines, TileCoord origin, std::size_t name_bytes) {
    if (registry.size() >= defines.max_player_cities) {
        return k_error_limit;
    }
    if (name_bytes > k_max_city_name_bytes) {
        return k_error_name;
    }
    const LoadedMap& map = *terrain.map();
    const CityArea area{origin};
    const std::vector<TileCoord> tiles = area.tiles();
    if (!std::all_of(tiles.begin(), tiles.end(),
                     [&](const TileCoord tile) { return map.contains_tile(tile); })) {
        return k_error_out_of_bounds;
    }
    if (!std::all_of(tiles.begin(), tiles.end(),
                     [&](const TileCoord tile) { return map.has_detail_tile(tile); })) {
        return k_error_no_detail;
    }
    if (registry.overlaps(area)) {
        return k_error_overlap;
    }
    if (land_fraction(map, area) < static_cast<double>(defines.min_land_fraction)) {
        return k_error_not_land;
    }
    return {};
}

void install_cities(core::Simulation& simulation, const CityDefines& defines) {
    validate(defines);
    core::World& world = simulation.world();
    auto& components = world.registry();

    core::Hooks<CityDefines> defines_hooks;
    defines_hooks.hash = &hash_defines;
    save::bind_serializer(defines_hooks);
    components.register_singleton<CityDefines>("world.CityDefines", defines_hooks);
    core::Hooks<CityRegistry> registry_hooks;
    registry_hooks.hash = &hash_registry;
    save::bind_serializer(registry_hooks);
    components.register_singleton<CityRegistry>("world.Cities", registry_hooks);
    core::Hooks<ActiveCity> active_hooks;
    active_hooks.hash = &hash_active;
    save::bind_serializer(active_hooks);
    components.register_singleton<ActiveCity>("world.ActiveCity", active_hooks);

    world.ctx().emplace<CityDefines>(defines);
    world.ctx().emplace<CityRegistry>();
    world.ctx().emplace<ActiveCity>();

    simulation.dispatcher().register_handler<FoundCity>(&found_city);
    simulation.dispatcher().register_handler<SetActiveCity>(&set_active_city);
}

} // namespace citysim::world
