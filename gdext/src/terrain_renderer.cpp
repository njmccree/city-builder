#include "terrain_renderer.hpp"

#include "city_sim.hpp"
#include "view_frame.hpp"

#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <limits>
#include <span>

namespace citysim::gdext {

namespace {

static_assert(sizeof(godot::Vector3) == 3 * sizeof(float),
              "the terrain writes Vector3 arrays as float triples");
static_assert(sizeof(godot::Color) == 4 * sizeof(float), "the terrain writes Color arrays as float quads");

using godot::RenderingServer;

constexpr double k_tile_m = world::k_tile_size_m;
// Horizon patches: the 32 m DEM layer at 64 m (32 quads per 2,048 m tile), the region layer at 1,024 m.
constexpr std::uint32_t k_dem_patch_quads = 32;
constexpr std::uint32_t k_region_patch_quads = 2;
constexpr float k_dem_skirt_m = 40.0F;
constexpr float k_region_skirt_m = 80.0F;
// How far a picking ray is marched per step before it is refined.
constexpr double k_pick_step_m = 2.0;

std::span<float> floats(godot::PackedVector3Array& array) {
    return {reinterpret_cast<float*>(array.ptrw()), static_cast<std::size_t>(array.size()) * 3};
}

std::span<float> floats(godot::PackedColorArray& array) {
    return {reinterpret_cast<float*>(array.ptrw()), static_cast<std::size_t>(array.size()) * 4};
}

godot::Array surface_arrays(const godot::PackedVector3Array& positions,
                            const godot::PackedVector3Array& normals, const godot::PackedColorArray& colors,
                            const godot::PackedInt32Array& indices) {
    godot::Array arrays;
    arrays.resize(godot::Mesh::ARRAY_MAX);
    arrays[godot::Mesh::ARRAY_VERTEX] = positions;
    arrays[godot::Mesh::ARRAY_NORMAL] = normals;
    arrays[godot::Mesh::ARRAY_COLOR] = colors;
    arrays[godot::Mesh::ARRAY_INDEX] = indices;
    return arrays;
}

// Turns vertex data made in the simulation's frame (z north) into the view frame (view_frame.hpp: z south),
// in place. Mirroring turns every triangle over, so the triangle lists are flipped too (flipped_winding).
void mirror_into_view(godot::PackedVector3Array& positions, godot::PackedVector3Array& normals) {
    godot::Vector3* p = positions.ptrw();
    godot::Vector3* n = normals.ptrw();
    for (int64_t i = 0; i < positions.size(); ++i) {
        p[i].z = view_z(p[i].z);
        n[i].z = view_z(n[i].z);
    }
}

// The same triangles with the opposite winding, which mirrored vertices need to stay front-facing.
godot::PackedInt32Array flipped_winding(const std::vector<std::int32_t>& indices, std::int32_t base = 0) {
    godot::PackedInt32Array out;
    out.resize(static_cast<int64_t>(indices.size()));
    std::int32_t* write = out.ptrw();
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        write[t] = base + indices[t];
        write[t + 1] = base + indices[t + 2];
        write[t + 2] = base + indices[t + 1];
    }
    return out;
}

// Appends one horizon patch to a layer's arrays, its origin moved to (east, north) meters from the city's
// south-west corner, in the view frame.
void append_patch(const world::ChunkVertices& patch, const std::vector<std::int32_t>& patch_indices,
                  float east, float north, godot::PackedVector3Array& positions,
                  godot::PackedVector3Array& normals, godot::PackedColorArray& colors,
                  godot::PackedInt32Array& indices) {
    const auto base = static_cast<std::int32_t>(positions.size());
    const std::size_t count = patch.positions.size() / 3;
    for (std::size_t v = 0; v < count; ++v) {
        positions.push_back(to_view(patch.positions[v * 3] + east, patch.positions[v * 3 + 1],
                                    patch.positions[v * 3 + 2] + north));
        normals.push_back(to_view(patch.normals[v * 3], patch.normals[v * 3 + 1], patch.normals[v * 3 + 2]));
        colors.push_back(godot::Color(patch.colors[v * 4], patch.colors[v * 4 + 1], patch.colors[v * 4 + 2],
                                      patch.colors[v * 4 + 3]));
    }
    indices.append_array(flipped_winding(patch_indices, base));
}

bool inside(world::TileCoord tile, world::TileCoord low, world::TileCoord high) {
    return tile.tx >= low.tx && tile.tx <= high.tx && tile.ty >= low.ty && tile.ty <= high.ty;
}

} // namespace

TerrainRenderer::TerrainRenderer() = default;

TerrainRenderer::~TerrainRenderer() = default;

void TerrainRenderer::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    using godot::PropertyInfo;
    using godot::Variant;
    ClassDB::bind_method(D_METHOD("set_sim_path", "path"), &TerrainRenderer::set_sim_path);
    ClassDB::bind_method(D_METHOD("get_sim_path"), &TerrainRenderer::get_sim_path);
    ClassDB::bind_method(D_METHOD("set_max_screen_error", "pixels"), &TerrainRenderer::set_max_screen_error);
    ClassDB::bind_method(D_METHOD("get_max_screen_error"), &TerrainRenderer::get_max_screen_error);
    ClassDB::bind_method(D_METHOD("set_split_distance_factor", "factor"),
                         &TerrainRenderer::set_split_distance_factor);
    ClassDB::bind_method(D_METHOD("get_split_distance_factor"), &TerrainRenderer::get_split_distance_factor);
    ClassDB::bind_method(D_METHOD("set_ring_min_lod", "lod"), &TerrainRenderer::set_ring_min_lod);
    ClassDB::bind_method(D_METHOD("get_ring_min_lod"), &TerrainRenderer::get_ring_min_lod);
    ClassDB::bind_method(D_METHOD("set_horizon_radius", "meters"), &TerrainRenderer::set_horizon_radius);
    ClassDB::bind_method(D_METHOD("get_horizon_radius"), &TerrainRenderer::get_horizon_radius);
    ClassDB::bind_method(D_METHOD("set_dem_horizon_tiles", "tiles"), &TerrainRenderer::set_dem_horizon_tiles);
    ClassDB::bind_method(D_METHOD("get_dem_horizon_tiles"), &TerrainRenderer::get_dem_horizon_tiles);
    ClassDB::bind_method(D_METHOD("set_upload_budget_ms", "ms"), &TerrainRenderer::set_upload_budget_ms);
    ClassDB::bind_method(D_METHOD("get_upload_budget_ms"), &TerrainRenderer::get_upload_budget_ms);
    ClassDB::bind_method(D_METHOD("set_max_chunk_meshes", "count"), &TerrainRenderer::set_max_chunk_meshes);
    ClassDB::bind_method(D_METHOD("get_max_chunk_meshes"), &TerrainRenderer::get_max_chunk_meshes);
    ClassDB::bind_method(D_METHOD("set_cast_shadows", "on"), &TerrainRenderer::set_cast_shadows);
    ClassDB::bind_method(D_METHOD("get_cast_shadows"), &TerrainRenderer::get_cast_shadows);
    ClassDB::bind_method(D_METHOD("has_terrain"), &TerrainRenderer::has_terrain);
    ClassDB::bind_method(D_METHOD("get_city_id"), &TerrainRenderer::get_city_id);
    ClassDB::bind_method(D_METHOD("get_city_origin_tile"), &TerrainRenderer::get_city_origin_tile);
    ClassDB::bind_method(D_METHOD("pick_terrain", "screen_pos"), &TerrainRenderer::pick_terrain);
    ClassDB::bind_method(D_METHOD("get_height_at", "x", "z"), &TerrainRenderer::get_height_at);
    ClassDB::bind_method(D_METHOD("get_stats"), &TerrainRenderer::get_stats);

    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sim_path"), "set_sim_path", "get_sim_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_screen_error"), "set_max_screen_error",
                 "get_max_screen_error");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "split_distance_factor"), "set_split_distance_factor",
                 "get_split_distance_factor");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ring_min_lod"), "set_ring_min_lod", "get_ring_min_lod");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "horizon_radius"), "set_horizon_radius", "get_horizon_radius");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "dem_horizon_tiles"), "set_dem_horizon_tiles",
                 "get_dem_horizon_tiles");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "upload_budget_ms"), "set_upload_budget_ms",
                 "get_upload_budget_ms");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_chunk_meshes"), "set_max_chunk_meshes",
                 "get_max_chunk_meshes");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cast_shadows"), "set_cast_shadows", "get_cast_shadows");
    ADD_SIGNAL(godot::MethodInfo("terrain_changed", PropertyInfo(Variant::INT, "city_id"),
                                 PropertyInfo(Variant::VECTOR2I, "origin_tile")));
}

void TerrainRenderer::_ready() {
    material_.instantiate();
    material_->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    material_->set_flag(godot::BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
    material_->set_roughness(1.0F);
    chunk_indices_ = flipped_winding(world::chunk_indices());
    set_process(true);
}

void TerrainRenderer::_exit_tree() {
    // Wait for the jobs: they write into Job objects this node owns.
    for (const int64_t id : task_ids_) {
        godot::WorkerThreadPool::get_singleton()->wait_for_task_completion(id);
    }
    task_ids_.clear();
    {
        const std::lock_guard<std::mutex> lock(jobs_mutex_);
        queued_.clear();
        finished_.clear();
    }
    jobs_in_flight_ = 0;
    clear_all();
}

void TerrainRenderer::set_cast_shadows(bool on) {
    cast_shadows_ = on;
    RenderingServer* rs = RenderingServer::get_singleton();
    const auto setting =
        on ? RenderingServer::SHADOW_CASTING_SETTING_ON : RenderingServer::SHADOW_CASTING_SETTING_OFF;
    for (auto& [coord, tile] : tiles_) {
        for (Chunk& chunk : tile.chunks) {
            if (chunk.instance.is_valid()) {
                rs->instance_geometry_set_cast_shadows_setting(chunk.instance, setting);
            }
        }
    }
}

godot::Vector2i TerrainRenderer::get_city_origin_tile() const {
    return godot::Vector2i(city_first_tile_.tx, city_first_tile_.ty);
}

// --- Jobs
// ------------------------------------------------------------------------------------------------------

void TerrainRenderer::submit(std::unique_ptr<Job> job) {
    int64_t id = 0;
    {
        const std::lock_guard<std::mutex> lock(jobs_mutex_);
        id = next_job_id_++;
        queued_.emplace(id, std::move(job));
    }
    ++jobs_in_flight_;
    task_ids_.push_back(godot::WorkerThreadPool::get_singleton()->add_task(
        callable_mp(this, &TerrainRenderer::run_job).bind(id), false, "terrain mesh"));
}

void TerrainRenderer::run_job(int64_t id) {
    std::unique_ptr<Job> job;
    {
        const std::lock_guard<std::mutex> lock(jobs_mutex_);
        const auto found = queued_.find(id);
        if (found == queued_.end()) {
            return;
        }
        job = std::move(found->second);
        queued_.erase(found);
    }
    switch (job->kind) {
    case JobKind::Analyse:
        job->info = std::make_shared<const world::TileLodInfo>(world::analyze_tile(*job->block[4]));
        if (job->previous) {
            job->changes = world::compare_tiles(*job->previous, *job->block[4]);
            job->compared = true;
        }
        break;
    case JobKind::Chunk: {
        std::array<const world::CityTile*, 9> block{};
        for (std::size_t i = 0; i < 9; ++i) {
            block[i] = job->block[i].get();
        }
        const world::TileHeights heights(block, job->map.get());
        job->positions.resize(world::k_chunk_vertex_count);
        job->normals.resize(world::k_chunk_vertex_count);
        job->colors.resize(world::k_chunk_vertex_count);
        // The deepest crack a skirt must hide is where this chunk meets the coarsest neighbor, whose error is
        // at most the tile's root error.
        const float skirt = job->info ? job->info->error_m[0] + 2.0F : 10.0F;
        world::build_chunk_vertices(heights, world::chunk_key(job->chunk), skirt, floats(job->positions),
                                    floats(job->normals), floats(job->colors));
        mirror_into_view(job->positions, job->normals);
        break;
    }
    case JobKind::Horizon:
        build_horizon(*job);
        break;
    }
    const std::lock_guard<std::mutex> lock(jobs_mutex_);
    finished_.push_back(std::move(job));
}

void TerrainRenderer::build_horizon(Job& job) {
    const world::LoadedMap& map = *job.map;
    const world::WorldPos city_origin = world::tile_origin(job.city_first_tile);
    // The hole is the active tiles' box; the 32 m DEM layer fills `dem_tiles` tiles around it, the region
    // layer the rest out to the radius.
    const world::TileCoord low = job.hole_low;
    const world::TileCoord high = job.hole_high;
    const std::int32_t dem = job.dem_tiles;
    const world::TileCoord dem_low{low.tx - dem, low.ty - dem};
    const world::TileCoord dem_high{high.tx + dem, high.ty + dem};
    const double centre_x = city_origin.x + world::k_city_area_size_m * 0.5;
    const double centre_z = city_origin.z + world::k_city_area_size_m * 0.5;
    const auto reach = static_cast<std::int32_t>(std::ceil(job.radius_m / k_tile_m));
    const world::TileCoord centre = world::tile_of(world::WorldPos{centre_x, centre_z});

    job.layer_positions.resize(2);
    job.layer_normals.resize(2);
    job.layer_colors.resize(2);
    job.layer_indices.resize(2);
    const std::vector<std::int32_t> dem_indices = world::patch_indices(k_dem_patch_quads);
    const std::vector<std::int32_t> region_indices = world::patch_indices(k_region_patch_quads);
    for (std::int32_t ty = centre.ty - reach; ty <= centre.ty + reach; ++ty) {
        for (std::int32_t tx = centre.tx - reach; tx <= centre.tx + reach; ++tx) {
            const world::TileCoord tile{tx, ty};
            if (!map.contains_tile(tile) || inside(tile, low, high)) {
                continue;
            }
            const world::WorldPos origin = world::tile_origin(tile);
            const auto east = static_cast<float>(origin.x - city_origin.x);
            const auto north = static_cast<float>(origin.z - city_origin.z);
            if (inside(tile, dem_low, dem_high)) {
                const world::ChunkVertices patch = world::build_horizon_patch(
                    map, origin, k_dem_patch_quads, k_tile_m / k_dem_patch_quads, k_dem_skirt_m);
                append_patch(patch, dem_indices, east, north, job.layer_positions[0], job.layer_normals[0],
                             job.layer_colors[0], job.layer_indices[0]);
            } else {
                const world::ChunkVertices patch = world::build_horizon_patch(
                    map, origin, k_region_patch_quads, k_tile_m / k_region_patch_quads, k_region_skirt_m);
                append_patch(patch, region_indices, east, north, job.layer_positions[1], job.layer_normals[1],
                             job.layer_colors[1], job.layer_indices[1]);
            }
        }
    }
}

// --- Per frame
// -------------------------------------------------------------------------------------------------

void TerrainRenderer::_process(double) {
    ++frame_;
    // Every task must be waited for once to be released; a finished one returns at once.
    godot::WorkerThreadPool* pool = godot::WorkerThreadPool::get_singleton();
    std::erase_if(task_ids_, [pool](int64_t id) {
        if (!pool->is_task_completed(id)) {
            return false;
        }
        pool->wait_for_task_completion(id);
        return true;
    });

    const CitySim* sim = godot::Object::cast_to<CitySim>(get_node_or_null(sim_path_));
    const runner::RenderSnapshot* snapshot =
        sim != nullptr && sim->snapshot_view().has_snapshot() ? &sim->snapshot_view().current() : nullptr;
    std::shared_ptr<const world::LoadedMap> map = sim != nullptr ? sim->loaded_map() : nullptr;
    const runner::SnapshotCity* city = nullptr;
    if (snapshot != nullptr && map) {
        for (const runner::SnapshotCity& candidate : snapshot->cities) {
            if (candidate.id == snapshot->active_city_id) {
                city = &candidate;
            }
        }
    }
    if (city == nullptr) {
        if (city_id_ != 0) {
            clear_all();
            emit_signal("terrain_changed", 0, godot::Vector2i());
        }
        return;
    }
    if (city->id != city_id_ || !(city->first_tile == city_first_tile_) || map != map_) {
        clear_all();
        map_ = map;
        city_id_ = city->id;
        city_first_tile_ = city->first_tile;
        emit_signal("terrain_changed", static_cast<int64_t>(city_id_), get_city_origin_tile());
    }
    sync_tiles(snapshot->terrain_tiles);
    apply_results();
    select_and_request();
    evict();
}

void TerrainRenderer::free_chunk(Chunk& chunk) {
    RenderingServer* rs = RenderingServer::get_singleton();
    if (chunk.instance.is_valid()) {
        rs->free_rid(chunk.instance);
    }
    if (chunk.mesh.is_valid()) {
        rs->free_rid(chunk.mesh);
    }
    chunk = Chunk{};
}

void TerrainRenderer::clear_all() {
    for (auto& [coord, tile] : tiles_) {
        for (Chunk& chunk : tile.chunks) {
            free_chunk(chunk);
        }
    }
    tiles_.clear();
    RenderingServer* rs = RenderingServer::get_singleton();
    for (const godot::RID& rid : horizon_instances_) {
        rs->free_rid(rid);
    }
    for (const godot::RID& rid : horizon_meshes_) {
        rs->free_rid(rid);
    }
    horizon_instances_.clear();
    horizon_meshes_.clear();
    horizon_triangles_ = 0;
    horizon_requested_ = false;
    city_id_ = 0;
    map_.reset();
    ++generation_; // results of jobs started before now are dropped
    chunks_drawn_ = 0;
    chunk_meshes_ = 0;
}

void TerrainRenderer::sync_tiles(const std::vector<runner::SnapshotTerrainTile>& tiles) {
    const world::CityArea area{city_first_tile_};
    std::vector<world::TileCoord> present;
    present.reserve(tiles.size());
    for (const runner::SnapshotTerrainTile& entry : tiles) {
        if (!entry.tile) {
            continue;
        }
        present.push_back(entry.coord);
        Tile& tile = tiles_[entry.coord];
        tile.ring = !area.contains_tile(entry.coord);
        if (tile.tile != entry.tile) {
            tile.tile = entry.tile;
        }
        if (!tile.analysing && tile.analysed != tile.tile) {
            auto job = std::make_unique<Job>();
            job->kind = JobKind::Analyse;
            job->generation = generation_;
            job->coord = entry.coord;
            job->block[4] = tile.tile;
            job->previous = tile.analysed;
            tile.analysing = true;
            submit(std::move(job));
        }
    }
    // Tiles that are no longer active.
    for (auto it = tiles_.begin(); it != tiles_.end();) {
        if (std::find(present.begin(), present.end(), it->first) == present.end()) {
            for (Chunk& chunk : it->second.chunks) {
                free_chunk(chunk);
            }
            it = tiles_.erase(it);
        } else {
            ++it;
        }
    }
    // The horizon leaves a hole exactly where the active tiles are, so it waits until they are known.
    if (!horizon_requested_ && !present.empty()) {
        active_min_ = present.front();
        active_max_ = present.front();
        for (const world::TileCoord coord : present) {
            active_min_ =
                world::TileCoord{std::min(active_min_.tx, coord.tx), std::min(active_min_.ty, coord.ty)};
            active_max_ =
                world::TileCoord{std::max(active_max_.tx, coord.tx), std::max(active_max_.ty, coord.ty)};
        }
        auto job = std::make_unique<Job>();
        job->kind = JobKind::Horizon;
        job->generation = generation_;
        job->map = map_;
        job->city_first_tile = city_first_tile_;
        job->hole_low = active_min_;
        job->hole_high = active_max_;
        job->dem_tiles = static_cast<std::int32_t>(std::clamp<int64_t>(dem_horizon_tiles_, 0, 64));
        job->radius_m = horizon_radius_m_;
        horizon_requested_ = true;
        submit(std::move(job));
    }
}

void TerrainRenderer::apply_results() {
    std::vector<std::unique_ptr<Job>> done;
    {
        const std::lock_guard<std::mutex> lock(jobs_mutex_);
        done.swap(finished_);
    }
    jobs_in_flight_ -= static_cast<int64_t>(done.size());
    const uint64_t start = godot::Time::get_singleton()->get_ticks_usec();
    const auto budget_us = static_cast<uint64_t>(std::max(upload_budget_ms_, 0.0) * 1000.0);
    std::vector<std::unique_ptr<Job>> later;
    for (std::unique_ptr<Job>& job : done) {
        if (job->generation != generation_) {
            continue; // made for another city
        }
        switch (job->kind) {
        case JobKind::Analyse:
            apply_analysis(*job);
            break;
        case JobKind::Horizon:
            apply_horizon(*job);
            break;
        case JobKind::Chunk:
            // Mesh uploads cost main-thread time: a few per frame, the rest next frame.
            if (godot::Time::get_singleton()->get_ticks_usec() - start > budget_us) {
                later.push_back(std::move(job));
            } else {
                apply_chunk(*job);
            }
            break;
        }
    }
    if (!later.empty()) {
        const std::lock_guard<std::mutex> lock(jobs_mutex_);
        jobs_in_flight_ += static_cast<int64_t>(later.size());
        for (std::unique_ptr<Job>& job : later) {
            finished_.push_back(std::move(job));
        }
    }
}

void TerrainRenderer::apply_analysis(Job& job) {
    const auto found = tiles_.find(job.coord);
    if (found == tiles_.end()) {
        return;
    }
    Tile& tile = found->second;
    tile.analysing = false;
    const world::CityTile* before = job.previous.get();
    const world::CityTile* now = job.block[4].get();
    // Chunks built from the previous version whose samples did not change are current as they are.
    for (std::uint32_t i = 0; i < world::k_tile_chunk_count; ++i) {
        Chunk& chunk = tile.chunks[i];
        if (job.compared && chunk.built_from == before && !job.changes.chunk_changed(world::chunk_key(i))) {
            chunk.built_from = now;
        }
    }
    tile.info = job.info;
    tile.analysed = job.block[4];
}

bool TerrainRenderer::apply_chunk(Job& job) {
    const auto found = tiles_.find(job.coord);
    if (found == tiles_.end()) {
        return false;
    }
    Tile& tile = found->second;
    Chunk& chunk = tile.chunks[job.chunk];
    chunk.building = false;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!chunk.mesh.is_valid()) {
        chunk.mesh = rs->mesh_create();
        chunk.instance = rs->instance_create2(chunk.mesh, get_world_3d()->get_scenario());
        rs->instance_set_transform(chunk.instance, tile_transform(job.coord));
        rs->instance_geometry_set_material_override(chunk.instance, material_->get_rid());
        rs->instance_geometry_set_cast_shadows_setting(
            chunk.instance, cast_shadows_ ? RenderingServer::SHADOW_CASTING_SETTING_ON
                                          : RenderingServer::SHADOW_CASTING_SETTING_OFF);
        rs->instance_set_visible(chunk.instance, false);
        chunk.visible = false;
    } else {
        rs->mesh_clear(chunk.mesh);
    }
    rs->mesh_add_surface_from_arrays(chunk.mesh, RenderingServer::PRIMITIVE_TRIANGLES,
                                     surface_arrays(job.positions, job.normals, job.colors, chunk_indices_));
    chunk.built_from = job.block[4].get();
    return true;
}

void TerrainRenderer::apply_horizon(Job& job) {
    RenderingServer* rs = RenderingServer::get_singleton();
    horizon_triangles_ = 0;
    for (std::size_t layer = 0; layer < job.layer_positions.size(); ++layer) {
        if (job.layer_indices[layer].is_empty()) {
            continue;
        }
        const godot::RID mesh = rs->mesh_create();
        rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES,
                                         surface_arrays(job.layer_positions[layer], job.layer_normals[layer],
                                                        job.layer_colors[layer], job.layer_indices[layer]));
        const godot::RID instance = rs->instance_create2(mesh, get_world_3d()->get_scenario());
        rs->instance_set_transform(instance, get_global_transform());
        rs->instance_geometry_set_material_override(instance, material_->get_rid());
        // Far away and coarse: its shadows would cost more than they show.
        rs->instance_geometry_set_cast_shadows_setting(instance, RenderingServer::SHADOW_CASTING_SETTING_OFF);
        rs->instance_set_visible(instance, is_visible_in_tree());
        horizon_meshes_.push_back(mesh);
        horizon_instances_.push_back(instance);
        horizon_triangles_ += job.layer_indices[layer].size() / 3;
    }
}

godot::Transform3D TerrainRenderer::tile_transform(world::TileCoord coord) const {
    const godot::Vector3 origin =
        to_view(static_cast<float>((coord.tx - city_first_tile_.tx) * k_tile_m), 0.0F,
                static_cast<float>((coord.ty - city_first_tile_.ty) * k_tile_m));
    return get_global_transform() * godot::Transform3D(godot::Basis(), origin);
}

void TerrainRenderer::select_and_request() {
    RenderingServer* rs = RenderingServer::get_singleton();
    godot::Viewport* viewport = get_viewport();
    godot::Camera3D* camera = viewport != nullptr ? viewport->get_camera_3d() : nullptr;
    const bool shown = is_visible_in_tree() && camera != nullptr;
    for (const godot::RID& instance : horizon_instances_) {
        rs->instance_set_visible(instance, is_visible_in_tree());
    }

    world::LodCamera base;
    godot::Vector3 eye;
    if (shown) {
        eye = get_global_transform().affine_inverse().xform(camera->get_global_position());
        const double height = std::max(1.0, static_cast<double>(viewport->get_visible_rect().size.y));
        const double half_fov = static_cast<double>(camera->get_fov()) * 0.5 * 3.14159265358979 / 180.0;
        base.pixels_per_radian = height / (2.0 * std::tan(half_fov));
        base.max_error_px = max_error_px_;
        base.split_distance_factor = split_distance_factor_;
    }

    struct Want {
        double priority = 0.0;
        world::TileCoord coord;
        std::uint32_t chunk = 0;
    };
    std::vector<Want> wanted;
    world::LodSelection selection;
    chunks_drawn_ = 0;
    chunk_meshes_ = 0;
    for (auto& [coord, tile] : tiles_) {
        const double ox = (coord.tx - city_first_tile_.tx) * k_tile_m;
        const double oz = (coord.ty - city_first_tile_.ty) * k_tile_m;
        for (const Chunk& chunk : tile.chunks) {
            chunk_meshes_ += chunk.mesh.is_valid() ? 1 : 0;
        }
        if (!shown || !tile.info) {
            continue;
        }
        world::LodCamera lod = base;
        lod.x = static_cast<double>(eye.x) - ox;
        lod.y = static_cast<double>(eye.y);
        lod.z = static_cast<double>(north_of_view(eye.z)) - oz;
        lod.min_lod = tile.ring ? static_cast<std::uint32_t>(std::clamp<int64_t>(ring_min_lod_, 0, 3)) : 0U;
        world::select_chunks(
            *tile.info, lod, [&tile](std::uint32_t i) { return tile.chunks[i].mesh.is_valid(); }, selection);
        const auto distance_to = [&](std::uint32_t i) {
            const world::ChunkKey key = world::chunk_key(i);
            const double size = world::chunk_size_m(key.lod);
            const double cx = (key.cx + 0.5) * size - lod.x;
            const double cz = (key.cy + 0.5) * size - lod.z;
            return std::sqrt(cx * cx + cz * cz);
        };
        for (const std::uint32_t i : selection.draw) {
            Chunk& chunk = tile.chunks[i];
            chunk.last_drawn = frame_;
            if (!chunk.visible) {
                rs->instance_set_visible(chunk.instance, true);
                chunk.visible = true;
            }
            ++chunks_drawn_;
            // Drawn but out of date (the tile changed under it): build it again, before anything new.
            if (chunk.built_from != tile.analysed.get() && !chunk.building) {
                wanted.push_back(Want{-1.0e9 + distance_to(i), coord, i});
            }
        }
        for (const std::uint32_t i : selection.wanted) {
            if (!tile.chunks[i].building) {
                // Coarse chunks first (they stand in for the rest), then the nearest.
                const double lod_rank = 3.0 - static_cast<double>(world::chunk_key(i).lod);
                wanted.push_back(Want{lod_rank * 1.0e6 + distance_to(i), coord, i});
            }
        }
    }
    // Hide what was drawn last frame and is not now.
    for (auto& [coord, tile] : tiles_) {
        for (Chunk& chunk : tile.chunks) {
            if (chunk.visible && chunk.last_drawn != frame_) {
                rs->instance_set_visible(chunk.instance, false);
                chunk.visible = false;
            }
        }
    }
    wanted_count_ = static_cast<int64_t>(wanted.size());
    std::sort(wanted.begin(), wanted.end(),
              [](const Want& a, const Want& b) { return a.priority < b.priority; });
    const int64_t limit =
        std::max<int64_t>(4, 2 * static_cast<int64_t>(godot::OS::get_singleton()->get_processor_count()));
    for (const Want& want : wanted) {
        if (jobs_in_flight_ >= limit) {
            break;
        }
        Tile& tile = tiles_[want.coord];
        auto job = std::make_unique<Job>();
        job->kind = JobKind::Chunk;
        job->generation = generation_;
        job->coord = want.coord;
        job->chunk = want.chunk;
        job->map = map_;
        job->info = tile.info;
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                const auto neighbor = tiles_.find(world::TileCoord{want.coord.tx + dx, want.coord.ty + dy});
                job->block[static_cast<std::size_t>((dy + 1) * 3 + (dx + 1))] =
                    neighbor != tiles_.end() ? neighbor->second.tile : nullptr;
            }
        }
        // Built from the version the LOD info describes, so the chunk and its error agree.
        job->block[4] = tile.analysed;
        tile.chunks[want.chunk].building = true;
        submit(std::move(job));
    }
}

void TerrainRenderer::evict() {
    if (chunk_meshes_ <= max_chunk_meshes_) {
        return;
    }
    struct Candidate {
        std::uint64_t last_drawn;
        world::TileCoord coord;
        std::uint32_t chunk;
    };
    std::vector<Candidate> candidates;
    for (auto& [coord, tile] : tiles_) {
        // The root of every tile stays: it is the stand-in for the rest.
        for (std::uint32_t i = 1; i < world::k_tile_chunk_count; ++i) {
            const Chunk& chunk = tile.chunks[i];
            if (chunk.mesh.is_valid() && !chunk.visible && !chunk.building) {
                candidates.push_back(Candidate{chunk.last_drawn, coord, i});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.last_drawn < b.last_drawn; });
    int64_t excess = chunk_meshes_ - max_chunk_meshes_;
    for (const Candidate& candidate : candidates) {
        if (excess <= 0) {
            break;
        }
        free_chunk(tiles_[candidate.coord].chunks[candidate.chunk]);
        --excess;
        --chunk_meshes_;
    }
}

// --- Queries
// ---------------------------------------------------------------------------------------------------

const world::CityTile* TerrainRenderer::tile_at_local(double x, double z, double& tile_x,
                                                      double& tile_z) const {
    if (!std::isfinite(x) || !std::isfinite(z)) {
        return nullptr;
    }
    const std::int32_t dx = world::tile_index_of(x);
    const std::int32_t dz = world::tile_index_of(z);
    const auto found = tiles_.find(world::TileCoord{city_first_tile_.tx + dx, city_first_tile_.ty + dz});
    if (found == tiles_.end() || !found->second.tile) {
        return nullptr;
    }
    tile_x = x - dx * k_tile_m;
    tile_z = z - dz * k_tile_m;
    return found->second.tile.get();
}

double TerrainRenderer::get_height_at(double x, double z) const {
    double tile_x = 0.0;
    double tile_z = 0.0;
    if (const world::CityTile* tile = tile_at_local(x, z, tile_x, tile_z)) {
        return tile->height_at(tile_x, tile_z);
    }
    if (!map_) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const world::WorldPos origin = world::tile_origin(city_first_tile_);
    return map_->dem_height(world::WorldPos{origin.x + x, origin.z + z});
}

godot::Variant TerrainRenderer::pick_terrain(const godot::Vector2& screen_pos) const {
    godot::Viewport* viewport = get_viewport();
    godot::Camera3D* camera = viewport != nullptr ? viewport->get_camera_3d() : nullptr;
    if (camera == nullptr || tiles_.empty()) {
        return godot::Variant();
    }
    const godot::Transform3D to_local = get_global_transform().affine_inverse();
    const godot::Vector3 from = to_local.xform(camera->project_ray_origin(screen_pos));
    const godot::Vector3 direction =
        to_local.basis.xform(camera->project_ray_normal(screen_pos)).normalized();

    // Clip the ray to the box around the active tiles, so the march covers only where there is ground.
    double low[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                     std::numeric_limits<double>::max()};
    double high[3] = {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
                      std::numeric_limits<double>::lowest()};
    for (const auto& [coord, tile] : tiles_) {
        const double ox = (coord.tx - city_first_tile_.tx) * k_tile_m;
        const double oz = (coord.ty - city_first_tile_.ty) * k_tile_m;
        low[0] = std::min(low[0], ox);
        high[0] = std::max(high[0], ox + k_tile_m);
        low[2] = std::min(low[2], oz);
        high[2] = std::max(high[2], oz + k_tile_m);
        const auto [min_h, max_h] =
            std::minmax_element(tile.tile->heights().begin(), tile.tile->heights().end());
        low[1] = std::min(low[1], static_cast<double>(*min_h) - 1.0);
        high[1] = std::max(high[1], static_cast<double>(*max_h) + 1.0);
    }
    // The ray in the simulation's frame (z north), where the tiles are.
    const double origin[3] = {from.x, from.y, north_of_view(from.z)};
    const double dir[3] = {direction.x, direction.y, north_of_view(direction.z)};
    double t0 = 0.0;
    double t1 = 1.0e7;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(dir[axis]) < 1e-12) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                return godot::Variant();
            }
            continue;
        }
        double a = (low[axis] - origin[axis]) / dir[axis];
        double b = (high[axis] - origin[axis]) / dir[axis];
        if (a > b) {
            std::swap(a, b);
        }
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
    }
    if (t0 > t1) {
        return godot::Variant();
    }
    const std::array<double, 3> start = {origin[0] + dir[0] * t0, origin[1] + dir[1] * t0,
                                         origin[2] + dir[2] * t0};
    const auto height = [this](double x, double z) -> std::optional<float> {
        double tile_x = 0.0;
        double tile_z = 0.0;
        const world::CityTile* tile = tile_at_local(x, z, tile_x, tile_z);
        if (tile == nullptr) {
            return std::nullopt;
        }
        return tile->height_at(tile_x, tile_z);
    };
    const std::optional<std::array<double, 3>> hit = world::raycast_heightfield(
        start, {dir[0], dir[1], dir[2]}, t1 - t0 + k_pick_step_m, k_pick_step_m, height);
    if (!hit) {
        return godot::Variant();
    }
    return godot::Vector3(static_cast<float>((*hit)[0]), static_cast<float>((*hit)[1]),
                          static_cast<float>((*hit)[2]));
}

godot::Dictionary TerrainRenderer::get_stats() const {
    godot::Dictionary stats;
    bool analysing = false;
    for (const auto& [coord, tile] : tiles_) {
        analysing = analysing || tile.analysing || !tile.info;
    }
    const int64_t chunk_triangles = static_cast<int64_t>(world::k_chunk_index_count / 3);
    stats["tiles"] = static_cast<int64_t>(tiles_.size());
    stats["chunks_drawn"] = chunks_drawn_;
    stats["chunk_meshes"] = chunk_meshes_;
    stats["triangles_drawn"] = chunks_drawn_ * chunk_triangles + horizon_triangles_;
    stats["horizon_triangles"] = horizon_triangles_;
    stats["jobs_pending"] = jobs_in_flight_;
    // Vertex buffers as Godot stores them (position 12 bytes, normal 4, color 4) plus 16-bit indices.
    stats["mesh_bytes"] = chunk_meshes_ * (static_cast<int64_t>(world::k_chunk_vertex_count) * 20 +
                                           static_cast<int64_t>(world::k_chunk_index_count) * 2);
    stats["ready"] = city_id_ != 0 && !analysing && wanted_count_ == 0 && jobs_in_flight_ == 0 &&
                     !horizon_instances_.empty();
    return stats;
}

} // namespace citysim::gdext
