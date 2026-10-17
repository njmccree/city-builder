#pragma once

#include <cstdint>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <vector>

namespace citysim::gdext {

// Shows the debug test map (architecture §11): a ground plane the size of the map, and every blocked cell as
// one box in a single MultiMesh. It reads the shared grid "testmap.grid" from CitySim's snapshot and rebuilds
// only when the grid is a different one from last time, so a stable map costs nothing per frame. Emits
// `map_changed(size)` with the map's extent in meters so the camera can set its bounds.
class TestMapView : public godot::Node3D {
    GDCLASS(TestMapView, godot::Node3D)

  protected:
    static void _bind_methods();

  public:
    void set_sim_path(const godot::NodePath& path) { sim_path_ = path; }
    godot::NodePath get_sim_path() const { return sim_path_; }

    // The map's extent in meters (x = width, y = depth); zero before a map has been seen.
    godot::Vector2 get_map_size() const { return map_size_; }
    // How many blocked cells are drawn.
    int64_t get_obstacle_count() const { return obstacle_count_; }

    void _ready() override;
    void _process(double delta) override;

  private:
    void rebuild(uint32_t width, uint32_t height, float cell_size, const std::vector<std::uint8_t>& cells);

    godot::NodePath sim_path_{"/root/Sim/CitySim"};
    godot::MeshInstance3D* ground_ = nullptr;
    godot::MultiMeshInstance3D* obstacles_ = nullptr;
    const void* shown_cells_ = nullptr; // identity of the grid on screen; never dereferenced
    godot::Vector2 map_size_;
    int64_t obstacle_count_ = 0;
};

} // namespace citysim::gdext
