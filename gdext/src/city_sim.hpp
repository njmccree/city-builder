#pragma once

#include "citysim/runner/game_session.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/translation.hpp>
#include <godot_cpp/variant/dictionary.hpp>
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
    // {ok: bool, errors: Array[String], warnings: Array[String]}.
    godot::Dictionary new_game(int64_t seed, const godot::PackedStringArray& content_roots,
                               const godot::PackedStringArray& enabled_mods, const godot::String& era_id);
    void shutdown();
    bool is_running() const;

    // Validates the command (see command_marshal.hpp) and queues it. Returns false, after push_error, if it
    // was not accepted. A true result means "queued", not "done": a save's outcome arrives as a signal.
    bool submit_command(const godot::Dictionary& command);

    // {tick, tps, tick_ms_avg, tick_ms_last, entity_count, speed, paused, date_text, time_text} from the
    // newest snapshot read by _process(); all zero/empty before a game starts.
    godot::Dictionary get_stats() const;

    void _process(double delta) override;

  private:
    void install_translations();
    void remove_translations();

    std::unique_ptr<runner::GameSession> session_;
    runner::RenderSnapshot last_;
    bool have_snapshot_ = false;
    godot::String date_text_;
    godot::String time_text_;
    std::vector<godot::Ref<godot::Translation>> translations_;
};

} // namespace citysim::gdext
