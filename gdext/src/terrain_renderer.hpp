#pragma once

#include "citysim/runner/render_snapshot.hpp"
#include "citysim/world/city_tile.hpp"
#include "citysim/world/loaded_map.hpp"
#include "citysim/world/terrain_mesh.hpp"
#include "citysim/world/tile_coord.hpp"

#include <array>
#include <cstdint>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace citysim::gdext {

// Draws the active city's terrain (architecture §15.9, §15.3): the 4 m tiles of the active city and its
// ring from the "terrain.tiles" snapshot data, and a coarse horizon beyond them from the map's 32 m DEM and
// region raster. Everything is in the active city's local frame (meters from its south-west corner, Godot
// x = east, z = north, y = height).
//
// Each tile is a quadtree of 85 chunks (world::terrain_mesh.hpp). Every frame the chunks are chosen by their
// geometric error seen from the camera; the meshes are built on WorkerThreadPool threads from the tile's
// immutable heights and handed to the RenderingServer on the main thread, a few per frame. A tile whose
// pointer changes (a terrain edit) is compared with the previous version on a worker and only the chunks
// that changed are built again; until then the old ones stay on screen. Ring tiles stop at a coarser LOD.
// Picking (pick_terrain) is a ray test against the same heights, in C++, so it needs no physics bodies.
//
// Renders on render time (_process), never on the simulation clock. Main thread only, except the jobs.
class TerrainRenderer : public godot::Node3D {
    GDCLASS(TerrainRenderer, godot::Node3D)

  protected:
    static void _bind_methods();

  public:
    TerrainRenderer();
    ~TerrainRenderer() override;

    void set_sim_path(const godot::NodePath& path) { sim_path_ = path; }
    godot::NodePath get_sim_path() const { return sim_path_; }
    void set_max_screen_error(double pixels) { max_error_px_ = pixels; }
    double get_max_screen_error() const { return max_error_px_; }
    void set_split_distance_factor(double factor) { split_distance_factor_ = factor; }
    double get_split_distance_factor() const { return split_distance_factor_; }
    void set_ring_min_lod(int64_t lod) { ring_min_lod_ = lod; }
    int64_t get_ring_min_lod() const { return ring_min_lod_; }
    void set_horizon_radius(double meters) { horizon_radius_m_ = meters; }
    double get_horizon_radius() const { return horizon_radius_m_; }
    void set_dem_horizon_tiles(int64_t tiles) { dem_horizon_tiles_ = tiles; }
    int64_t get_dem_horizon_tiles() const { return dem_horizon_tiles_; }
    void set_upload_budget_ms(double ms) { upload_budget_ms_ = ms; }
    double get_upload_budget_ms() const { return upload_budget_ms_; }
    void set_max_chunk_meshes(int64_t count) { max_chunk_meshes_ = count; }
    int64_t get_max_chunk_meshes() const { return max_chunk_meshes_; }
    void set_cast_shadows(bool on);
    bool get_cast_shadows() const { return cast_shadows_; }

    // True while an active city's terrain is shown (some tile has been drawn).
    bool has_terrain() const { return city_id_ != 0; }
    // The active city's id, and the south-west tile of its area (its local frame's origin).
    int64_t get_city_id() const { return city_id_; }
    godot::Vector2i get_city_origin_tile() const;

    // The ground under a point of the screen, in the active city's local frame (x, height, z), or null where
    // the ray misses the active tiles.
    godot::Variant pick_terrain(const godot::Vector2& screen_pos) const;
    // The ground height at a local point: the active tiles, else the map's 32 m DEM / region raster; NaN
    // without a map.
    double get_height_at(double x, double z) const;
    // {tiles, chunks_drawn, chunk_meshes, triangles_drawn, horizon_triangles, jobs_pending, mesh_bytes,
    //  ready}: what is on screen, for the perf overlay and tests. `ready` is true when every wanted chunk is
    // built and no job is pending.
    godot::Dictionary get_stats() const;

    void _ready() override;
    void _process(double delta) override;
    void _exit_tree() override;

  private:
    struct Chunk {
        godot::RID mesh;
        godot::RID instance;
        const world::CityTile* built_from = nullptr; // identity only; never dereferenced
        bool building = false;
        bool visible = false;
        std::uint64_t last_drawn = 0;
    };
    struct Tile {
        std::shared_ptr<const world::CityTile> tile;     // the newest from the snapshot
        std::shared_ptr<const world::CityTile> analysed; // the one `info` describes
        std::shared_ptr<const world::TileLodInfo> info;
        bool analysing = false;
        bool ring = false;
        std::array<Chunk, world::k_tile_chunk_count> chunks;
    };
    enum class JobKind { Analyse, Chunk, Horizon };
    struct Job {
        JobKind kind = JobKind::Chunk;
        std::uint64_t generation = 0;
        world::TileCoord coord;
        std::uint32_t chunk = 0;
        std::array<std::shared_ptr<const world::CityTile>, 9> block; // Chunk: the tile and its neighbors
        std::shared_ptr<const world::CityTile> previous;             // Analyse: the version before
        std::shared_ptr<const world::LoadedMap> map;
        world::TileCoord city_first_tile; // Horizon: the city, the hole
        world::TileCoord hole_low;        // (the active tiles' box), how
        world::TileCoord hole_high;       // many 32 m DEM tiles around it,
        std::int32_t dem_tiles = 0;       // and how far the region layer
        double radius_m = 0.0;            // reaches
        // Results.
        std::shared_ptr<const world::TileLodInfo> info;
        world::TileChanges changes;
        bool compared = false;
        godot::PackedVector3Array positions;
        godot::PackedVector3Array normals;
        godot::PackedColorArray colors;
        godot::PackedInt32Array indices; // Horizon only (chunks share chunk_indices_)
        std::vector<godot::PackedVector3Array> layer_positions;
        std::vector<godot::PackedVector3Array> layer_normals;
        std::vector<godot::PackedColorArray> layer_colors;
        std::vector<godot::PackedInt32Array> layer_indices;
    };

    void run_job(int64_t id);
    void submit(std::unique_ptr<Job> job);
    static void build_horizon(Job& job);

    void clear_all();
    void free_chunk(Chunk& chunk);
    void sync_tiles(const std::vector<runner::SnapshotTerrainTile>& tiles);
    void apply_results();
    void apply_analysis(Job& job);
    bool apply_chunk(Job& job);
    void apply_horizon(Job& job);
    void select_and_request();
    void evict();
    godot::Transform3D tile_transform(world::TileCoord coord) const;
    const world::CityTile* tile_at_local(double x, double z, double& tile_x, double& tile_z) const;

    godot::NodePath sim_path_{"/root/Sim/CitySim"};
    double max_error_px_ = 2.0;
    double split_distance_factor_ = 1.5;
    int64_t ring_min_lod_ = 1;
    double horizon_radius_m_ = 40000.0;
    int64_t dem_horizon_tiles_ = 4;
    double upload_budget_ms_ = 3.0;
    int64_t max_chunk_meshes_ = 1200;
    bool cast_shadows_ = true;

    std::shared_ptr<const world::LoadedMap> map_;
    std::uint32_t city_id_ = 0;
    world::TileCoord city_first_tile_;
    std::uint64_t generation_ = 0;
    std::uint64_t frame_ = 0;
    std::map<world::TileCoord, Tile> tiles_;
    world::TileCoord active_min_; // the active tiles' bounding box, for the horizon's hole
    world::TileCoord active_max_;
    std::vector<godot::RID> horizon_meshes_;
    std::vector<godot::RID> horizon_instances_;
    int64_t horizon_triangles_ = 0;
    bool horizon_requested_ = false;

    godot::Ref<godot::StandardMaterial3D> material_;
    godot::PackedInt32Array chunk_indices_;

    // Jobs: queued here by the main thread, taken by a worker, finished ones handed back.
    mutable std::mutex jobs_mutex_;
    std::map<int64_t, std::unique_ptr<Job>> queued_;
    std::vector<std::unique_ptr<Job>> finished_;
    std::vector<int64_t> task_ids_;
    int64_t next_job_id_ = 1;
    int64_t jobs_in_flight_ = 0;

    // Stats of the last frame.
    int64_t chunks_drawn_ = 0;
    int64_t chunk_meshes_ = 0;
    int64_t wanted_count_ = 0;
};

} // namespace citysim::gdext
