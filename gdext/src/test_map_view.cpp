#include "test_map_view.hpp"

#include "city_sim.hpp"
#include "citysim/debug/agent_snapshot.hpp"
#include "view_frame.hpp"

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <vector>

namespace citysim::gdext {

namespace {

constexpr float k_obstacle_height = 3.0F;
// The ground sits a hair above y = 0 so it wins over the big temporary ground plane under it.
constexpr float k_ground_lift = 0.03F;
constexpr int64_t k_floats_per_instance = 12;

} // namespace

void TestMapView::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_sim_path", "path"), &TestMapView::set_sim_path);
    godot::ClassDB::bind_method(godot::D_METHOD("get_sim_path"), &TestMapView::get_sim_path);
    godot::ClassDB::bind_method(godot::D_METHOD("get_map_size"), &TestMapView::get_map_size);
    godot::ClassDB::bind_method(godot::D_METHOD("get_obstacle_count"), &TestMapView::get_obstacle_count);
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::NODE_PATH, "sim_path"), "set_sim_path", "get_sim_path");
    ADD_SIGNAL(godot::MethodInfo("map_changed", godot::PropertyInfo(godot::Variant::VECTOR2, "size")));
}

void TestMapView::_ready() {
    ground_ = memnew(godot::MeshInstance3D);
    ground_->set_name("Ground");
    godot::Ref<godot::StandardMaterial3D> ground_material;
    ground_material.instantiate();
    ground_material->set_albedo(godot::Color(0.42F, 0.55F, 0.36F));
    ground_->set_material_override(ground_material);
    add_child(ground_);

    obstacles_ = memnew(godot::MultiMeshInstance3D);
    obstacles_->set_name("Obstacles");
    godot::Ref<godot::StandardMaterial3D> obstacle_material;
    obstacle_material.instantiate();
    obstacle_material->set_albedo(godot::Color(0.35F, 0.35F, 0.4F));
    obstacles_->set_material_override(obstacle_material);
    add_child(obstacles_);
    set_process(true);
}

void TestMapView::_process(double) {
    const CitySim* sim = godot::Object::cast_to<CitySim>(get_node_or_null(sim_path_));
    if (sim == nullptr || !sim->snapshot_view().has_snapshot()) {
        return;
    }
    const runner::SnapshotGrid* grid = sim->snapshot_view().current().find_grid(debug::k_test_map_grid);
    if (grid == nullptr || grid->cells == nullptr) {
        return;
    }
    if (grid->cells.get() == shown_cells_) {
        return;
    }
    rebuild(grid->width, grid->height, grid->cell_size, *grid->cells);
    shown_cells_ = grid->cells.get();
}

void TestMapView::rebuild(uint32_t width, uint32_t height, float cell_size,
                          const std::vector<std::uint8_t>& cells) {
    map_size_ = godot::Vector2(static_cast<float>(width) * cell_size, static_cast<float>(height) * cell_size);

    godot::Ref<godot::PlaneMesh> plane;
    plane.instantiate();
    plane->set_size(map_size_);
    ground_->set_mesh(plane);
    ground_->set_position(to_view(map_size_.x * 0.5F, k_ground_lift, map_size_.y * 0.5F));

    int64_t blocked = 0;
    for (const std::uint8_t cell : cells) {
        blocked += cell == 0 ? 1 : 0;
    }
    godot::Ref<godot::MultiMesh> multimesh;
    multimesh.instantiate();
    multimesh->set_transform_format(godot::MultiMesh::TRANSFORM_3D);
    godot::Ref<godot::BoxMesh> box;
    box.instantiate();
    box->set_size(godot::Vector3(1.0F, 1.0F, 1.0F));
    multimesh->set_mesh(box);
    multimesh->set_instance_count(static_cast<int32_t>(blocked));

    godot::PackedFloat32Array buffer;
    buffer.resize(blocked * k_floats_per_instance);
    float* out = buffer.ptrw();
    int64_t written = 0;
    for (uint32_t index = 0; index < cells.size(); ++index) {
        if (cells[index] != 0) {
            continue;
        }
        const float x = (static_cast<float>(index % width) + 0.5F) * cell_size;
        const float z = (static_cast<float>(index / width) + 0.5F) * cell_size;
        float* row = out + written * k_floats_per_instance;
        // Scaled to the cell footprint and the obstacle height; the box is centered, so lift it by half.
        row[0] = cell_size;
        row[1] = 0.0F;
        row[2] = 0.0F;
        row[3] = x;
        row[4] = 0.0F;
        row[5] = k_obstacle_height;
        row[6] = 0.0F;
        row[7] = k_obstacle_height * 0.5F + k_ground_lift;
        row[8] = 0.0F;
        row[9] = 0.0F;
        row[10] = cell_size;
        row[11] = view_z(z);
        ++written;
    }
    if (blocked > 0) {
        multimesh->set_buffer(buffer);
    }
    obstacles_->set_multimesh(multimesh);
    obstacle_count_ = blocked;
    emit_signal("map_changed", map_size_);
}

} // namespace citysim::gdext
