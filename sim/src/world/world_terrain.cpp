#include "citysim/world/world_terrain.hpp"

#include "citysim/core/job_system.hpp"
#include "citysim/profiling/profile.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace citysim::world {

namespace {

const std::string k_no_map_id;

} // namespace

WorldTerrain::WorldTerrain(std::shared_ptr<const LoadedMap> map, std::uint64_t map_seed)
    : map_(std::move(map)), map_seed_(map_seed) {
    if (!map_) {
        throw std::invalid_argument("WorldTerrain needs a map");
    }
}

const std::string& WorldTerrain::map_id() const {
    return map_ ? map_->map_id() : k_no_map_id;
}

std::uint64_t WorldTerrain::package_hash() const {
    return map_ ? map_->package_hash() : 0;
}

const LoadedMap& WorldTerrain::require_map() const {
    if (!map_) {
        throw std::logic_error("WorldTerrain has no map");
    }
    return *map_;
}

void WorldTerrain::activate_tiles(core::JobSystem& jobs, std::span<const TileCoord> tiles) {
    CS_PROFILE_ZONE("world.activate_tiles");
    const LoadedMap& map = require_map();
    std::vector<TileCoord> missing;
    for (const TileCoord tile : tiles) {
        if (!map.contains_tile(tile)) {
            throw std::out_of_range("tile (" + std::to_string(tile.tx) + ", " + std::to_string(tile.ty) +
                                    ") lies outside the map");
        }
        if (!tiles_.contains(tile)) {
            missing.push_back(tile);
        }
    }
    std::sort(missing.begin(), missing.end());
    missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
    if (missing.empty()) {
        return;
    }

    // One chunk per tile; each writes only its own slot, and the slots are added in order afterwards
    // (architecture §7.2). If a tile fails, the lowest failing one is rethrown and nothing is added.
    std::vector<std::shared_ptr<const CityTile>> made(missing.size());
    const std::uint64_t seed = map_seed_;
    jobs.parallel_for(missing.size(), 1, [&](std::size_t begin, std::size_t end, std::size_t) {
        for (std::size_t i = begin; i < end; ++i) {
            made[i] = TerrainSynth::make_tile(map, missing[i], seed);
        }
    });
    for (std::size_t i = 0; i < missing.size(); ++i) {
        tiles_.emplace(missing[i], std::move(made[i]));
    }
}

void WorldTerrain::deactivate_tiles(std::span<const TileCoord> tiles) {
    for (const TileCoord tile : tiles) {
        tiles_.erase(tile);
    }
}

std::vector<TileCoord> WorldTerrain::active_tiles() const {
    std::vector<TileCoord> out;
    out.reserve(tiles_.size());
    for (const auto& entry : tiles_) {
        out.push_back(entry.first);
    }
    return out;
}

std::shared_ptr<const CityTile> WorldTerrain::tile(TileCoord tile) const {
    const auto it = tiles_.find(tile);
    return it == tiles_.end() ? nullptr : it->second;
}

float WorldTerrain::height_at(WorldPos pos) const {
    const LoadedMap& map = require_map();
    if (!tiles_.empty() && std::isfinite(pos.x) && std::isfinite(pos.z)) {
        // The same tie-break as the detail DEM: on an edge, the first active tile that has the point.
        const LoadedMap::Candidates candidates = LoadedMap::covering_tiles(pos);
        for (std::size_t i = 0; i < candidates.count; ++i) {
            const auto it = tiles_.find(candidates.tiles[i]);
            if (it != tiles_.end()) {
                const WorldPos origin = tile_origin(it->first);
                return it->second->height_at(pos.x - origin.x, pos.z - origin.z);
            }
        }
    }
    return map.dem_height(pos);
}

float WorldTerrain::height_at_local(const CityArea& city, LocalPos pos) const {
    return height_at(to_world(city, pos));
}

void WorldTerrain::gradient(WorldPos pos, float& dx, float& dz) const {
    constexpr double k_step = k_city_sample_spacing_m;
    const float east = height_at(WorldPos{pos.x + k_step, pos.z});
    const float west = height_at(WorldPos{pos.x - k_step, pos.z});
    const float north = height_at(WorldPos{pos.x, pos.z + k_step});
    const float south = height_at(WorldPos{pos.x, pos.z - k_step});
    dx = (east - west) / static_cast<float>(2.0 * k_step);
    dz = (north - south) / static_cast<float>(2.0 * k_step);
}

float WorldTerrain::slope_at(WorldPos pos) const {
    float dx = 0.0F;
    float dz = 0.0F;
    gradient(pos, dx, dz);
    return std::sqrt(dx * dx + dz * dz);
}

TerrainNormal WorldTerrain::normal_at(WorldPos pos) const {
    float dx = 0.0F;
    float dz = 0.0F;
    gradient(pos, dx, dz);
    // The surface y = h(x, z) has the upward normal (-dh/dx, 1, -dh/dz), normalized.
    const float length = std::sqrt(dx * dx + 1.0F + dz * dz);
    return TerrainNormal{-dx / length, 1.0F / length, -dz / length};
}

SavedTerrain WorldTerrain::saved_state() const {
    SavedTerrain saved;
    saved.map_id = map_id();
    saved.package_hash = package_hash();
    saved.generator_version = generator_version_;
    saved.active_tiles = active_tiles();
    return saved;
}

std::optional<SavedTerrain> WorldTerrain::take_staged_restore() {
    std::optional<SavedTerrain> out = std::move(staged_restore_);
    staged_restore_.reset();
    return out;
}

} // namespace citysim::world
