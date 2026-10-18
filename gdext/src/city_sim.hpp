#pragma once

#include "citysim/runner/game_session.hpp"
#include "citysim/runner/snapshot_view.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/translation.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <memory>
#include <vector>

namespace citysim::gdext {

// The game's connection to the C++ simulation (architecture §11), registered as the autoload `Sim`'s core.
// It owns a GameSession (content + SimulationRunner on its own thread) and, once per frame in _process(),
// reads the newest snapshot and turns changes into signals. GDScript sees only this class: it submits
// commands as Dictionaries (validated in C++) and reads stats; it never touches simulation state. Main thread
// only.
class CitySim : public godot::Node {
    GDCLASS(CitySim, godot::Node)

  protected:
    static void _bind_methods();

  public:
    CitySim();
    ~CitySim() override;

    godot::String get_core_version() const;

    // Loads the content, starts a game and its simulation thread. Replaces a running game. Returns
    // {ok: bool, errors: Array[String], warnings: Array[String]}. `test_map_size` above 0 installs the debug
    // agents on a square test map that many cells wide. `world_agents` (with a `map_id`, no test map)
    // installs the debug agents on the real terrain of the active city.
    godot::Dictionary new_game(int64_t seed, const godot::PackedStringArray& content_roots,
                               const godot::PackedStringArray& enabled_mods, const godot::String& era_id,
                               int64_t test_map_size = 0, const godot::String& map_id = godot::String(),
                               bool world_agents = false);
    void shutdown();
    bool is_running() const;

    // Validates the command (see command_marshal.hpp) and queues it. Returns false, after push_error, if it
    // was not accepted. A true result means "queued", not "done": a save's outcome arrives as a signal.
    bool submit_command(const godot::Dictionary& command);

    // {tick, tps, tick_ms_avg, tick_ms_last, entity_count, speed, paused, date_text, time_text} from the
    // newest snapshot read by _process(); all zero/empty before a game starts.
    godot::Dictionary get_stats() const;

    // How far between the previous and the current snapshot it is time to draw this frame, 0..1 (1 while
    // paused or when no game runs). Computed once per frame in _process(), so every reader in a frame agrees.
    float get_interpolation_alpha() const { return alpha_; }

    // A snapshot channel by name as a flat array (see SnapshotChannel), for debugging; empty if there is
    // none. Fast renderers use snapshot_view() instead and never copy.
    godot::PackedFloat32Array get_channel(const godot::String& name) const;

    // The founded cities from the newest snapshot, in founding order: [{id, name, origin_tx, origin_ty,
    // active}], and the id of the active one (0 for none). Empty without a map (new_game's `map_id`).
    godot::Array get_cities() const;
    int64_t get_active_city_id() const { return static_cast<int64_t>(last_.active_city_id); }

    // For C++ renderer nodes: the previous and current snapshot, and the alpha to blend them with.
    const runner::SnapshotView& snapshot_view() const { return view_; }

    void _process(double delta) override;

  private:
    void install_translations();
    void remove_translations();

    std::unique_ptr<runner::GameSession> session_;
    runner::RenderSnapshot last_;
    bool have_snapshot_ = false;
    runner::SnapshotView view_;
    float alpha_ = 1.0F;
    godot::String date_text_;
    godot::String time_text_;
    std::vector<godot::Ref<godot::Translation>> translations_;
};

} // namespace citysim::gdext
