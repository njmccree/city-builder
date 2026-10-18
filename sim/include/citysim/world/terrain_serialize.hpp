#pragma once

// Save/load of the terrain's state (architecture §10, §15.4; pattern in citysim/save/archive.hpp). Field
// order is the save format: append with a version bump, never reorder.

#include "citysim/save/archive.hpp"
#include "citysim/world/terraform_commands.hpp"
#include "citysim/world/terrain_deltas.hpp"
#include "citysim/world/tile_coord.hpp"
#include "citysim/world/water_commands.hpp"
#include "citysim/world/water_state.hpp"
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

// The river level offsets, one per reach. The reader refuses a value that is not a finite number.
template <class Ar> void serialize(Ar& ar, WaterState& water, std::uint32_t version) {
    std::vector<float> offsets(water.level_offsets().begin(), water.level_offsets().end());
    save::io(ar, offsets, version);
    if constexpr (Ar::is_loading) {
        try {
            water.restore(std::move(offsets));
        } catch (const std::invalid_argument& error) {
            ar.fail(error.what());
        }
    }
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

template <class Ar> void serialize(Ar& ar, SetRiverLevel& command, std::uint32_t version) {
    save::io(ar, command.reach_index, version);
    save::io(ar, command.offset_m, version);
}

template <class Ar> void serialize(Ar& ar, Terraform& command, std::uint32_t version) {
    save::io(ar, command.city_id, version);
    auto op = static_cast<std::uint8_t>(command.op);
    save::io(ar, op, version);
    if constexpr (Ar::is_loading) {
        if (op >= k_terraform_op_count) {
            ar.fail("a saved Terraform command has an unknown operation");
        }
        command.op = static_cast<TerraformOp>(op);
    }
    save::io(ar, command.center.x, version);
    save::io(ar, command.center.z, version);
    save::io(ar, command.radius_m, version);
    save::io(ar, command.strength, version);
    save::io(ar, command.target_height_m, version);
}

} // namespace citysim::world
