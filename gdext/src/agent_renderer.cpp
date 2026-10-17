#include "agent_renderer.hpp"

#include "city_sim.hpp"
#include "citysim/debug/agent_snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/capsule_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace citysim::gdext {

namespace {

constexpr float k_pi = 3.14159265358979F;
constexpr float k_two_pi = 6.28318530717959F;
constexpr float k_agent_height = 1.8F;
// A MultiMesh transform is 12 floats: the three rows of the basis, each followed by that axis of the origin.
constexpr int64_t k_floats_per_instance = 12;

// The angle from `from` to `to` the short way round, in -pi..pi.
float shortest_arc(float from, float to) {
    float difference = std::fmod(to - from + k_pi, k_two_pi);
    if (difference < 0.0F) {
        difference += k_two_pi;
    }
    return difference - k_pi;
}

} // namespace

void AgentRenderer::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_sim_path", "path"), &AgentRenderer::set_sim_path);
    godot::ClassDB::bind_method(godot::D_METHOD("get_sim_path"), &AgentRenderer::get_sim_path);
    godot::ClassDB::bind_method(godot::D_METHOD("get_agent_count"), &AgentRenderer::get_agent_count);
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::NODE_PATH, "sim_path"), "set_sim_path", "get_sim_path");
}

void AgentRenderer::_ready() {
    multimesh_.instantiate();
    multimesh_->set_transform_format(godot::MultiMesh::TRANSFORM_3D);
    godot::Ref<godot::CapsuleMesh> mesh;
    mesh.instantiate();
    mesh->set_radius(0.3F);
    mesh->set_height(k_agent_height);
    // The default capsule has about a thousand triangles; at 10,000 agents that alone costs more than a
    // frame.
    mesh->set_radial_segments(8);
    mesh->set_rings(2);
    multimesh_->set_mesh(mesh);
    set_multimesh(multimesh_);

    godot::Ref<godot::StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(godot::Color(0.95F, 0.55F, 0.15F));
    set_material_override(material);
    // A crowd this size does not need shadows, and drawing it a second time for them would double the cost.
    set_cast_shadows_setting(godot::GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);

    // The engine cannot know where the instances are, so give it a box that is always in view.
    set_custom_aabb(
        godot::AABB(godot::Vector3(-100.0F, -10.0F, -100.0F), godot::Vector3(8192.0F, 40.0F, 8192.0F)));
    set_process(true);
}

void AgentRenderer::ensure_capacity(int64_t needed) {
    if (needed <= capacity_) {
        return;
    }
    // Headroom, so a growing crowd does not reallocate every frame.
    capacity_ = std::max<int64_t>(needed + needed / 4, 256);
    multimesh_->set_instance_count(static_cast<int32_t>(capacity_));
    buffer_.resize(capacity_ * k_floats_per_instance);
}

void AgentRenderer::_process(double) {
    const CitySim* sim = godot::Object::cast_to<CitySim>(get_node_or_null(sim_path_));
    if (sim == nullptr || multimesh_.is_null()) {
        return;
    }
    const runner::SnapshotView& view = sim->snapshot_view();
    const runner::SnapshotChannel* current =
        view.has_snapshot() ? view.current().find_channel(debug::k_agent_channel) : nullptr;
    if (current == nullptr || current->stride != 4 || current->ids.size() != current->record_count()) {
        count_ = 0;
        if (capacity_ > 0) {
            multimesh_->set_visible_instance_count(0);
        }
        return;
    }
    const runner::SnapshotChannel* previous = view.previous().find_channel(debug::k_agent_channel);
    const bool can_blend =
        previous != nullptr && previous->stride == 4 && previous->ids.size() == previous->record_count();
    const float alpha = sim->get_interpolation_alpha();

    const auto agents = static_cast<int64_t>(current->record_count());
    ensure_capacity(agents);
    float* out = buffer_.ptrw();

    // Both channels list agents by ascending id, so one pass with two cursors pairs them up.
    std::size_t earlier = 0;
    for (int64_t i = 0; i < agents; ++i) {
        const float* now = &current->data[static_cast<std::size_t>(i) * 4];
        float x = now[0];
        float z = now[2];
        float yaw = now[3];
        if (can_blend) {
            const std::uint32_t id = current->ids[static_cast<std::size_t>(i)];
            while (earlier < previous->ids.size() && previous->ids[earlier] < id) {
                ++earlier;
            }
            if (earlier < previous->ids.size() && previous->ids[earlier] == id) {
                const float* before = &previous->data[earlier * 4];
                x = before[0] + (now[0] - before[0]) * alpha;
                z = before[2] + (now[2] - before[2]) * alpha;
                yaw = before[3] + shortest_arc(before[3], now[3]) * alpha;
            }
        }
        const float sine = std::sin(yaw);
        const float cosine = std::cos(yaw);
        float* row = out + i * k_floats_per_instance;
        // Rotation about the vertical axis; the origin is lifted by half the height so the feet touch the
        // ground.
        row[0] = cosine;
        row[1] = 0.0F;
        row[2] = sine;
        row[3] = x;
        row[4] = 0.0F;
        row[5] = 1.0F;
        row[6] = 0.0F;
        row[7] = k_agent_height * 0.5F;
        row[8] = -sine;
        row[9] = 0.0F;
        row[10] = cosine;
        row[11] = z;
    }
    multimesh_->set_buffer(buffer_);
    multimesh_->set_visible_instance_count(static_cast<int32_t>(agents));
    count_ = agents;
}

} // namespace citysim::gdext
