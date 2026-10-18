#pragma once

// Save/load of the terrain's state (architecture §10, §15.4; pattern in citysim/save/archive.hpp). Field
// order is the save format: append with a version bump, never reorder.

#include "citysim/save/archive.hpp"
#include "citysim/world/terrain_deltas.hpp"
#include "citysim/world/tile_coord.hpp"
#include "citysim/world/world_terrain.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace citysim::world {

template <class Ar> void serialize(Ar& ar, TileCoord& tile, std::uint32_t version) {
    save::io(ar, tile.tx, version);
    save::io(ar, tile.ty, version);
}

// A delta layer travels as its tile and one zstd frame of the offsets, so a layer that is mostly zeros stays
// small. The reader refuses a frame that is not exactly one layer of finite offsets with a non-zero one.
template <class Ar> void serialize(Ar& ar, SavedTileDelta& delta, std::uint32_t version) {
    save::io(ar, delta.coord, version);
    std::vector<std::uint8_t> packed;
    if constexpr (!Ar::is_loading) {
        packed = compress_offsets(delta.offsets);
    }
    save::io(ar, packed, version);
    if constexpr (Ar::is_loading) {
        try {
            delta.offsets = decompress_offsets(packed);
        } catch (const std::invalid_argument& error) {
            ar.fail(error.what());
        }
    }
}

template <class Ar> void serialize(Ar& ar, SavedTerrain& saved, std::uint32_t version) {
    save::io(ar, saved.map_id, version);
    save::io(ar, saved.package_hash, version);
    save::io(ar, saved.generator_version, version);
    save::io(ar, saved.active_tiles, version);
    save::io(ar, saved.deltas, version);
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
        try {
            static_cast<void>(
                TerrainDeltas::from_saved(saved.deltas)); // refuse bad layers here, with an offset
        } catch (const std::invalid_argument& error) {
            ar.fail(error.what());
        }
        terrain.stage_restore(std::move(saved));
    }
}

} // namespace citysim::world
