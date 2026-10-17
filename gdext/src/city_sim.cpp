#include "city_sim.hpp"

#include "citysim/content/date_text.hpp"
#include "citysim/core/version.hpp"
#include "command_marshal.hpp"

#include <filesystem>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/translation_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <string>

namespace citysim::gdext {

namespace {

using godot::Dictionary;
using godot::String;

String to_godot(const std::string& text) {
    return String::utf8(text.c_str(), static_cast<int64_t>(text.size()));
}

std::string to_std(const String& text) {
    const godot::CharString utf8 = text.utf8();
    return std::string(utf8.get_data(), static_cast<std::size_t>(utf8.length()));
}

// res:// and user:// paths become real folders (the simulation side uses plain file paths).
std::filesystem::path real_path(const String& path) {
    return std::filesystem::path(to_std(godot::ProjectSettings::get_singleton()->globalize_path(path)));
}

String describe(const content::ContentIssue& issue) {
    return to_godot(issue.file.empty() ? issue.message : issue.format_full());
}

String two_digits(unsigned value) {
    return String::num_int64(value).pad_zeros(2);
}

} // namespace

CitySim::CitySim() = default;

CitySim::~CitySim() {
    shutdown();
}

void CitySim::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("get_core_version"), &CitySim::get_core_version);
    godot::ClassDB::bind_method(
        godot::D_METHOD("new_game", "seed", "content_roots", "enabled_mods", "era_id"), &CitySim::new_game);
    godot::ClassDB::bind_method(godot::D_METHOD("shutdown"), &CitySim::shutdown);
    godot::ClassDB::bind_method(godot::D_METHOD("is_running"), &CitySim::is_running);
    godot::ClassDB::bind_method(godot::D_METHOD("submit_command", "command"), &CitySim::submit_command);
    godot::ClassDB::bind_method(godot::D_METHOD("get_stats"), &CitySim::get_stats);

    ADD_SIGNAL(godot::MethodInfo("tick_advanced", godot::PropertyInfo(godot::Variant::INT, "tick")));
    ADD_SIGNAL(godot::MethodInfo("date_changed", godot::PropertyInfo(godot::Variant::STRING, "text")));
    ADD_SIGNAL(godot::MethodInfo("speed_changed", godot::PropertyInfo(godot::Variant::INT, "speed"),
                                 godot::PropertyInfo(godot::Variant::BOOL, "paused")));
    ADD_SIGNAL(godot::MethodInfo("save_completed", godot::PropertyInfo(godot::Variant::STRING, "path"),
                                 godot::PropertyInfo(godot::Variant::BOOL, "ok"),
                                 godot::PropertyInfo(godot::Variant::STRING, "message")));
    ADD_SIGNAL(godot::MethodInfo("load_completed", godot::PropertyInfo(godot::Variant::STRING, "path"),
                                 godot::PropertyInfo(godot::Variant::BOOL, "ok"),
                                 godot::PropertyInfo(godot::Variant::STRING, "message")));
}

godot::String CitySim::get_core_version() const {
    const std::string_view version = core::version();
    return godot::String::utf8(version.data(), static_cast<int>(version.size()));
}

Dictionary CitySim::new_game(int64_t seed, const godot::PackedStringArray& content_roots,
                             const godot::PackedStringArray& enabled_mods, const String& era_id) {
    shutdown();

    runner::NewGameRequest request;
    request.seed = static_cast<std::uint64_t>(seed);
    for (int64_t i = 0; i < content_roots.size(); ++i) {
        request.content_roots.push_back(real_path(content_roots[i]));
    }
    for (int64_t i = 0; i < enabled_mods.size(); ++i) {
        request.enabled_mods.push_back(to_std(enabled_mods[i]));
    }
    request.era_id = to_std(era_id);
    request.autosave_directory = real_path("user://saves/autosave");

    runner::NewGameOutcome outcome = runner::start_new_game(request);

    godot::Array errors;
    for (const content::ContentIssue& issue : outcome.errors) {
        errors.push_back(describe(issue));
    }
    godot::Array warnings;
    for (const content::ContentIssue& issue : outcome.warnings) {
        warnings.push_back(describe(issue));
    }
    Dictionary result;
    result["ok"] = outcome.ok();
    result["errors"] = errors;
    result["warnings"] = warnings;

    if (outcome.ok()) {
        session_ = std::move(outcome.session);
        have_snapshot_ = false;
        install_translations();
        set_process(true);
    }
    return result;
}

void CitySim::shutdown() {
    if (!session_) {
        return;
    }
    set_process(false);
    remove_translations();
    session_.reset(); // stops the simulation thread
    have_snapshot_ = false;
    last_ = runner::RenderSnapshot();
    date_text_ = String();
    time_text_ = String();
}

bool CitySim::is_running() const {
    return session_ != nullptr;
}

bool CitySim::submit_command(const Dictionary& command) {
    if (!session_) {
        godot::UtilityFunctions::push_error(
            "CitySim.submit_command: no game is running; call new_game() first");
        return false;
    }
    MarshalContext context;
    context.content = &session_->content();
    const MarshalResult result = marshal_command(command, context);
    if (!result.command) {
        godot::UtilityFunctions::push_error("CitySim.submit_command: ", to_godot(result.error));
        return false;
    }
    session_->runner().submit(*result.command);
    return true;
}

Dictionary CitySim::get_stats() const {
    Dictionary stats;
    stats["tick"] = static_cast<int64_t>(last_.tick);
    stats["tps"] = last_.stats.ticks_per_second;
    stats["tick_ms_avg"] = last_.stats.avg_tick_ms;
    stats["tick_ms_last"] = last_.stats.last_tick_ms;
    stats["entity_count"] = static_cast<int64_t>(last_.stats.entity_count);
    stats["speed"] = static_cast<int64_t>(last_.speed);
    stats["paused"] = last_.paused;
    stats["date_text"] = date_text_;
    stats["time_text"] = time_text_;
    return stats;
}

void CitySim::_process(double) {
    if (!session_) {
        return;
    }
    runner::RenderSnapshot snapshot = session_->latest_snapshot();

    // Results of saves and loads (each is handed over once).
    for (const runner::UiEvent& event : snapshot.ui_events) {
        if (event.kind == runner::UiEventKind::Message) {
            continue;
        }
        String message = to_godot(event.text);
        if (event.ok && !event.warnings.empty()) {
            std::string joined;
            for (const std::string& warning : event.warnings) {
                joined += (joined.empty() ? "" : "\n") + warning;
            }
            message = to_godot(joined);
        }
        const char* signal =
            event.kind == runner::UiEventKind::SaveCompleted ? "save_completed" : "load_completed";
        emit_signal(signal, to_godot(event.path), event.ok, message);
    }

    // What changed since the last frame, decided before anything is emitted.
    const content::Localizer& localizer = session_->content().localizer();
    const bool first = !have_snapshot_;
    const bool tick_changed = first || snapshot.tick != last_.tick;
    const bool pace_changed = first || snapshot.speed != last_.speed || snapshot.paused != last_.paused;
    const bool day_changed = snapshot.has_date && (first || !last_.has_date || snapshot.date != last_.date);
    if (day_changed) {
        date_text_ = to_godot(content::format_date_text(localizer, snapshot.date));
    }
    if (snapshot.has_date) {
        time_text_ =
            to_godot(localizer.format("calendar.time", {{"hour", to_std(two_digits(snapshot.hour))},
                                                        {"minute", to_std(two_digits(snapshot.minute))}}));
    }
    last_ = std::move(snapshot);
    have_snapshot_ = true;

    // Listeners find the new numbers in get_stats() when they hear about them.
    if (tick_changed) {
        emit_signal("tick_advanced", static_cast<int64_t>(last_.tick));
    }
    if (day_changed) {
        emit_signal("date_changed", date_text_);
    }
    if (pace_changed) {
        emit_signal("speed_changed", static_cast<int64_t>(last_.speed), last_.paused);
    }
}

// The UI uses tr("key"): give Godot a Translation per loaded language, built from the Localizer's tables.
void CitySim::install_translations() {
    remove_translations();
    const content::Localizer& localizer = session_->content().localizer();
    godot::TranslationServer* server = godot::TranslationServer::get_singleton();
    for (const std::string& language : localizer.languages()) {
        godot::Ref<godot::Translation> translation;
        translation.instantiate();
        translation->set_locale(to_godot(language));
        for (const std::string& key : localizer.keys(language)) {
            // Looked up through get() with this language current would change it; read the table directly.
            translation->add_message(godot::StringName(to_godot(key)),
                                     godot::StringName(to_godot(localizer.text(language, key))));
        }
        server->add_translation(translation);
        translations_.push_back(translation);
    }
}

void CitySim::remove_translations() {
    godot::TranslationServer* server = godot::TranslationServer::get_singleton();
    if (server != nullptr) {
        for (const godot::Ref<godot::Translation>& translation : translations_) {
            server->remove_translation(translation);
        }
    }
    translations_.clear();
}

} // namespace citysim::gdext
