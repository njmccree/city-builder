#include "command_marshal.hpp"

#include "citysim/content/era_def.hpp"
#include "citysim/time/time_commands.hpp"

#include <array>
#include <cmath>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <initializer_list>
#include <string_view>

namespace citysim::gdext {

namespace {

using godot::Dictionary;
using godot::String;
using godot::Variant;

std::string to_std(const String& text) {
    const godot::CharString utf8 = text.utf8();
    return std::string(utf8.get_data(), static_cast<std::size_t>(utf8.length()));
}

MarshalResult failure(std::string message) {
    MarshalResult result;
    result.error = std::move(message);
    return result;
}

MarshalResult success(core::Command command) {
    MarshalResult result;
    result.command = std::move(command);
    return result;
}

// Rejects any key that is not in `allowed`, so a typo is an error instead of being ignored.
bool only_keys(const Dictionary& dictionary, std::initializer_list<std::string_view> allowed,
               std::string& error) {
    const godot::Array keys = dictionary.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        const Variant key = keys[i];
        const std::string name = key.get_type() == Variant::STRING ? to_std(String(key)) : "(not a string)";
        bool known = false;
        for (const std::string_view candidate : allowed) {
            known = known || candidate == name;
        }
        if (!known) {
            error = "unknown field '" + name + "'";
            return false;
        }
    }
    return true;
}

// A whole number: an int, or a float that has no fraction (what JSON-ish sources give).
bool read_int(const Dictionary& dictionary, const char* key, int64_t& out, std::string& error) {
    if (!dictionary.has(key)) {
        error = std::string("missing field '") + key + "'";
        return false;
    }
    const Variant value = dictionary[key];
    if (value.get_type() == Variant::INT) {
        out = static_cast<int64_t>(value);
        return true;
    }
    if (value.get_type() == Variant::FLOAT) {
        const double number = static_cast<double>(value);
        if (std::floor(number) == number && std::fabs(number) < 9.0e15) {
            out = static_cast<int64_t>(number);
            return true;
        }
    }
    error = std::string("field '") + key + "' must be a whole number";
    return false;
}

// Any number, as a double.
bool read_number(const Dictionary& dictionary, const char* key, double& out, std::string& error) {
    if (!dictionary.has(key)) {
        error = std::string("missing field '") + key + "'";
        return false;
    }
    const Variant value = dictionary[key];
    if (value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT) {
        out = static_cast<double>(value);
        if (std::isfinite(out)) {
            return true;
        }
    }
    error = std::string("field '") + key + "' must be a number";
    return false;
}

bool read_string(const Dictionary& dictionary, const char* key, std::string& out, std::string& error) {
    if (!dictionary.has(key)) {
        error = std::string("missing field '") + key + "'";
        return false;
    }
    const Variant value = dictionary[key];
    if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
        error = std::string("field '") + key + "' must be a string";
        return false;
    }
    out = to_std(String(value));
    if (out.empty()) {
        error = std::string("field '") + key + "' must not be empty";
        return false;
    }
    return true;
}

// A save path as the game uses it (res://, user://) becomes a real file path; its folder is created for
// saves.
std::string file_path(const std::string& path, bool create_folder) {
    const String globalized =
        godot::ProjectSettings::get_singleton()->globalize_path(String::utf8(path.c_str()));
    if (create_folder) {
        godot::DirAccess::make_dir_recursive_absolute(globalized.get_base_dir());
    }
    return to_std(globalized);
}

MarshalResult parse_set_speed(const Dictionary& dictionary, const MarshalContext& context) {
    std::string error;
    int64_t speed = 0;
    if (!only_keys(dictionary, {"type", "speed"}, error) || !read_int(dictionary, "speed", speed, error)) {
        return failure(error);
    }
    const int64_t highest =
        context.content != nullptr
            ? static_cast<int64_t>(context.content->time_defines().speed_multipliers.size()) - 1
            : 3;
    if (speed < 1 || speed > highest) {
        return failure("'speed' must be between 1 and " + std::to_string(highest));
    }
    return success(time::SetSpeed{static_cast<std::uint8_t>(speed)});
}

MarshalResult parse_toggle_pause(const Dictionary& dictionary, const MarshalContext&) {
    std::string error;
    if (!only_keys(dictionary, {"type"}, error)) {
        return failure(error);
    }
    return success(time::TogglePause{});
}

MarshalResult parse_set_era(const Dictionary& dictionary, const MarshalContext& context) {
    std::string error;
    std::string era_id;
    if (!only_keys(dictionary, {"type", "era_id"}, error) ||
        !read_string(dictionary, "era_id", era_id, error)) {
        return failure(error);
    }
    if (context.content != nullptr && !context.content->registry<content::EraDef>().find(era_id)) {
        return failure("no era has the id '" + era_id + "'");
    }
    return success(time::SetEra{era_id});
}

MarshalResult parse_save_game(const Dictionary& dictionary, const MarshalContext&) {
    std::string error;
    std::string path;
    if (!only_keys(dictionary, {"type", "path"}, error) || !read_string(dictionary, "path", path, error)) {
        return failure(error);
    }
    return success(core::SaveGame{file_path(path, true)});
}

MarshalResult parse_load_game(const Dictionary& dictionary, const MarshalContext&) {
    std::string error;
    std::string path;
    if (!only_keys(dictionary, {"type", "path"}, error) || !read_string(dictionary, "path", path, error)) {
        return failure(error);
    }
    return success(core::LoadGame{file_path(path, false)});
}

MarshalResult parse_state_hash(const Dictionary& dictionary, const MarshalContext&) {
    std::string error;
    if (!only_keys(dictionary, {"type"}, error)) {
        return failure(error);
    }
    return success(core::RequestStateHash{});
}

MarshalResult parse_spawn_agents(const Dictionary& dictionary, const MarshalContext&) {
    std::string error;
    int64_t count = 0;
    if (!only_keys(dictionary, {"type", "count"}, error) || !read_int(dictionary, "count", count, error)) {
        return failure(error);
    }
    constexpr int64_t k_most_agents = 1000000;
    if (count < 1 || count > k_most_agents) {
        return failure("'count' must be between 1 and " + std::to_string(k_most_agents));
    }
    return success(debug::SpawnDebugAgents{static_cast<std::uint32_t>(count)});
}

MarshalResult parse_clear_agents(const Dictionary& dictionary, const MarshalContext&) {
    std::string error;
    if (!only_keys(dictionary, {"type"}, error)) {
        return failure(error);
    }
    return success(debug::ClearDebugAgents{});
}

MarshalResult parse_set_time_scale(const Dictionary& dictionary, const MarshalContext&) {
    std::string error;
    double scale = 0.0;
    int64_t day_ticks = 0;
    if (!only_keys(dictionary, {"type", "movement_scale", "day_length_ticks"}, error) ||
        !read_number(dictionary, "movement_scale", scale, error) ||
        !read_int(dictionary, "day_length_ticks", day_ticks, error)) {
        return failure(error);
    }
    if (!(scale > 0.0) || scale > 100.0) {
        return failure("'movement_scale' must be above 0 and at most 100");
    }
    if (day_ticks < 24 || day_ticks > 10000000) {
        return failure("'day_length_ticks' must be between 24 and 10000000");
    }
    return success(time::SetTimeScale{static_cast<float>(scale), static_cast<std::uint32_t>(day_ticks)});
}

// The conversion table: one line per command type.
struct Entry {
    std::string_view type;
    MarshalResult (*parse)(const Dictionary&, const MarshalContext&);
};
constexpr std::array<Entry, 9> k_entries{{
    {"set_speed", &parse_set_speed},
    {"toggle_pause", &parse_toggle_pause},
    {"set_era", &parse_set_era},
    {"save_game", &parse_save_game},
    {"load_game", &parse_load_game},
    {"state_hash", &parse_state_hash},
    {"spawn_agents", &parse_spawn_agents},
    {"clear_agents", &parse_clear_agents},
    {"set_time_scale", &parse_set_time_scale},
}};

} // namespace

MarshalResult marshal_command(const Dictionary& dictionary, const MarshalContext& context) {
    if (!dictionary.has("type")) {
        return failure("missing field 'type'");
    }
    const Variant type_value = dictionary["type"];
    if (type_value.get_type() != Variant::STRING && type_value.get_type() != Variant::STRING_NAME) {
        return failure("field 'type' must be a string");
    }
    const std::string type = to_std(String(type_value));
    for (const Entry& entry : k_entries) {
        if (entry.type == type) {
            return entry.parse(dictionary, context);
        }
    }
    std::string known;
    for (const Entry& entry : k_entries) {
        known += (known.empty() ? "" : ", ") + std::string(entry.type);
    }
    return failure("unknown command type '" + type + "' (known: " + known + ")");
}

} // namespace citysim::gdext
