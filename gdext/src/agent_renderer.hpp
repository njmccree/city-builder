#pragma once

#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

namespace citysim::gdext {

// Draws the debug agents (architecture §7.3, §11): every frame it takes the previous and the current snapshot
// and the interpolation alpha from CitySim and writes one transform per agent straight into a MultiMesh
// buffer. Agents are matched between the two snapshots by id, so ones that appeared or vanished in between
// are drawn where they are (or skipped) instead of jumping. There is no per-agent Godot call and no GDScript
// loop. Runs on render time (_process), reads only snapshots, and never touches the simulation.
class AgentRenderer : public godot::MultiMeshInstance3D {
    GDCLASS(AgentRenderer, godot::MultiMeshInstance3D)

  protected:
    static void _bind_methods();

  public:
    // Where the CitySim node is; the default is the one inside the `Sim` autoload.
    void set_sim_path(const godot::NodePath& path) { sim_path_ = path; }
    godot::NodePath get_sim_path() const { return sim_path_; }

    // How many agents were drawn in the last frame.
    int64_t get_agent_count() const { return count_; }

    void _ready() override;
    void _process(double delta) override;

  private:
    // Grows the MultiMesh (with headroom) so it can hold `needed` instances; this discards its contents.
    void ensure_capacity(int64_t needed);

    godot::NodePath sim_path_{"/root/Sim/CitySim"};
    godot::Ref<godot::MultiMesh> multimesh_;
    godot::PackedFloat32Array buffer_;
    int64_t capacity_ = 0;
    int64_t count_ = 0;
};

} // namespace citysim::gdext
