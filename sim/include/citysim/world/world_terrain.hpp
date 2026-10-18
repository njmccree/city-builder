#pragma once

#include "citysim/world/city_tile.hpp"
#include "citysim/world/frames.hpp"
#include "citysim/world/loaded_map.hpp"
#include "citysim/world/terrain_deltas.hpp"
#include "citysim/world/terrain_synth.hpp"
#include "citysim/world/tile_coord.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace citysim::core {
class JobSystem;
}

namespace citysim::world {

// What a save holds of the terrain (architecture §15.4): which map, which data and generator made it, which
// tiles were active, and the terrain edits (height offsets, see terrain_deltas.hpp). Never base heights:
// those are synthesized again on load.
struct SavedTerrain {
    std::string map_id;
    std::uint64_t package_hash = 0;
    std::uint32_t generator_version = 0;
    std::vector<TileCoord> active_tiles; // sorted, no repeats
    // The terrain edits (architecture §15.4): one layer per edited tile, sorted by tile, no empty layers.
    std::vector<SavedTileDelta> deltas;

    friend bool operator==(const SavedTerrain&, const SavedTerrain&) = default;
};

// A surface normal (unit length, y up; x east and z north like the world frame).
struct TerrainNormal {
    float x = 0.0F;
    float y = 1.0F;
    float z = 0.0F;
};

// The simulation's terrain service (architecture §15): a World ctx() singleton, registered as
// "world.Terrain" by world::install(). It holds the map in use (shared, immutable LoadedMap) and the cache of
// synthesized 4 m city tiles, and answers height, slope and normal queries anywhere on the map.
//
// State: the hashed and saved part is exactly SavedTerrain (map id, package hash, generator version, the set
// of active tiles, the terrain edits). The tiles' base heights are derived from it, so they are neither
// hashed nor saved; a load synthesizes them again (world::install's after-load hook) and adds the edits.
// The edits are the only heights that are saved: sparse per-tile offsets (TerrainDeltas), kept also for tiles
// that are not active and applied again when such a tile is activated.
//
// Clock: none of its own. Tiles change only when a caller (founding a city, switching cities, loading)
// activates or deactivates them, between or inside ticks, from the simulation thread.
//
// Threading: activate_tiles() and deactivate_tiles() change the cache and must not run while anything
// queries it. The queries are const and safe to call from parallel_for chunks at the same time.
class WorldTerrain {
  public:
    // No map: what a load starts from before the saved data arrives. Queries need a map.
    WorldTerrain() = default;
    WorldTerrain(std::shared_ptr<const LoadedMap> map, std::uint64_t map_seed);

    bool has_map() const { return map_ != nullptr; }
    const std::shared_ptr<const LoadedMap>& map() const { return map_; }
    // The map's id and package hash; empty and 0 without a map.
    const std::string& map_id() const;
    std::uint64_t package_hash() const;
    // The generator that made the cached tiles: always k_terrain_generator_version in this build.
    std::uint32_t generator_version() const { return generator_version_; }
    // From the MapDef: drives micro-detail, so a map is the same in every game (never the game seed).
    std::uint64_t map_seed() const { return map_seed_; }

    // Synthesizes the tiles that are not active yet (TerrainSynth::make_tile) on `jobs`, one chunk per tile,
    // and adds them in TileCoord order once all are done (architecture §7.2), so the result is identical for
    // any worker count. A tile that has edits gets them added to its synthesized heights. Repeats and tiles
    // already active are ignored. Throws std::out_of_range, before changing anything, if a tile lies outside
    // the map; std::logic_error without a map.
    void activate_tiles(core::JobSystem& jobs, std::span<const TileCoord> tiles);
    // Drops the tiles from the cache; tiles that are not active are ignored. Snapshots that still hold a tile
    // keep it alive until they are gone. Their edits are kept.
    void deactivate_tiles(std::span<const TileCoord> tiles);
    void deactivate_all() {
        tiles_.clear();
        bases_.clear();
    }

    bool is_active(TileCoord tile) const { return tiles_.contains(tile); }
    std::vector<TileCoord> active_tiles() const; // sorted
    // The active tiles by coordinate (sorted), for publishing.
    const std::map<TileCoord, std::shared_ptr<const CityTile>>& tiles() const { return tiles_; }
    // The active tile, or nullptr.
    std::shared_ptr<const CityTile> tile(TileCoord tile) const;

    // Height in meters above sea level at a world point: bilinear in an active city tile where one covers
    // the point (on an edge between two, either: they agree), otherwise the 32 m detail DEM, otherwise the
    // region elevation (LoadedMap::dem_height). Terrain edits will be added on top (T1.1.2.3). Throws
    // std::logic_error without a map.
    float height_at(WorldPos pos) const;
    // The same, at a point in a city's local frame.
    float height_at_local(const CityArea& city, LocalPos pos) const;
    // The steepness at a point as rise over run (the tangent of the slope angle; 1.0 is 45 degrees), from
    // central differences of height_at() 4 m (one city sample) to each side.
    float slope_at(WorldPos pos) const;
    // The upward unit normal at a point, from the same differences as slope_at().
    TerrainNormal normal_at(WorldPos pos) const;

    // What an edit function decides for one sample: given its coordinates in the tile (0..512, row 0 at the
    // south) and its current height in meters, the height it should have. Must be deterministic and give a
    // finite number.
    using HeightFn = std::function<float(std::uint32_t sx, std::uint32_t sy, float current_height)>;
    // Edits the heights of the samples in `rect` of an active tile (architecture §15.4). The new height of a
    // sample is stored as an offset from the synthesized base, in the tile's delta layer (made on the first
    // edit). The tile is replaced by a new immutable CityTile with version + 1, so only edited tiles get a
    // new pointer. A sample on a tile edge is shared with the neighbor(s): they get the same offset, so edges
    // stay equal; a neighbor that is not active keeps it in its layer until it is activated. Throws
    // std::invalid_argument, before changing anything, for an invalid rect or a non-finite height, and
    // std::logic_error if the tile is not active. Call it from the simulation thread, not while queries run.
    void apply_delta(TileCoord tile, SampleRect rect, const HeightFn& fn);
    // The edits, also those of tiles that are not active.
    const TerrainDeltas& deltas() const { return deltas_; }

    // The part that is hashed and saved.
    SavedTerrain saved_state() const;

    // Loading: the serializer hands the saved part over here, and world::install's after-load hook takes it
    // and restores the terrain (restore_terrain in world_module.hpp). Not part of the state.
    void stage_restore(SavedTerrain saved) { staged_restore_ = std::move(saved); }
    // Replaces the edits (restore_terrain, before it activates the saved tiles). Throws std::invalid_argument
    // for layers TerrainDeltas::from_saved refuses. Tiles that are active are not changed: call it with none.
    void restore_deltas(std::vector<SavedTileDelta> saved);
    std::optional<SavedTerrain> take_staged_restore();

  private:
    const LoadedMap& require_map() const;
    // Central differences: (dh/dx, dh/dz).
    void gradient(WorldPos pos, float& dx, float& dz) const;

    std::shared_ptr<const LoadedMap> map_;
    std::uint64_t map_seed_ = 0;
    std::uint32_t generator_version_ = k_terrain_generator_version;
    std::map<TileCoord, std::shared_ptr<const CityTile>> tiles_;
    // Active tiles that have a delta layer, as synthesized: the heights before the edits. An active tile
    // without a layer is its own base.
    std::map<TileCoord, std::shared_ptr<const CityTile>> bases_;
    TerrainDeltas deltas_;
    std::optional<SavedTerrain> staged_restore_;
};

} // namespace citysim::world
