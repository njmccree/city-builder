#pragma once

// Save/load of the terrain's state (architecture §10, §15.4; pattern in citysim/save/archive.hpp). Field
// order is the save format: append with a version bump, never reorder.

#include "citysim/save/archive.hpp"
#include "citysim/world/tile_coord.hpp"
#include "citysim/world/world_terrain.hpp"

#include <algorithm>
#include <utility>

namespace citysim::world {

template <class Ar> void serialize(Ar& ar, TileCoord& tile, std::uint32_t version) {
    save::io(ar, tile.tx, version);
    save::io(ar, tile.ty, version);
}

template <class Ar> void serialize(Ar& ar, SavedTerrain& saved, std::uint32_t version) {
    save::io(ar, saved.map_id, version);
    save::io(ar, saved.package_hash, version);
    save::io(ar, saved.generator_version, version);
    save::io(ar, saved.active_tiles, version);
}

// Only SavedTerrain travels: never heights. Loading hands it to WorldTerrain::stage_restore(); the terrain is
// rebuilt from it after the whole World has loaded (world::install's after-load hook).
template <class Ar> void serialize(Ar& ar, WorldTerrain& terrain, std::uint32_t version) {
    SavedTerrain saved;
    if constexpr (!Ar::is_loading) {
        saved = terrain.saved_state();
    }
    save::io(ar, saved, version);
    if constexpr (Ar::is_loading) {
        const auto& tiles = saved.active_tiles;
        if (std::adjacent_find(tiles.begin(), tiles.end(), [](const TileCoord& a, const TileCoord& b) {
                return !(a < b);
            }) != tiles.end()) {
            ar.fail("the active terrain tiles are not sorted or repeat");
        }
        terrain.stage_restore(std::move(saved));
    }
}

} // namespace citysim::world
