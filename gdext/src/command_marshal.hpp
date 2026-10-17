#pragma once

#include "citysim/content/content_database.hpp"
#include "citysim/core/commands.hpp"

#include <godot_cpp/variant/dictionary.hpp>
#include <optional>
#include <string>

namespace citysim::gdext {

// What the validation of a command may look things up in.
struct MarshalContext {
    const content::ContentDatabase* content = nullptr;
};

struct MarshalResult {
    std::optional<core::Command> command; // set on success
    std::string error;                    // set on failure: says which field is wrong and why
};

// Turns a command written as a Dictionary, as GDScript submits it, into a typed command, validating every
// field in C++ (architecture §5.3, §11). The Dictionary has a "type" and that type's fields; unknown types,
// missing, extra or wrongly typed fields and out-of-range values are all errors.
//
//   {"type": "set_speed", "speed": 2}                  speed 1 .. the number of speeds
//   {"type": "toggle_pause"}
//   {"type": "set_era", "era_id": "base:era.modern"}   must be an era in the loaded content
//   {"type": "save_game", "path": "user://saves/a.csav"}
//   {"type": "load_game", "path": "user://saves/a.csav"}
//
// HOW TO ADD A COMMAND: write a parse function in command_marshal.cpp and add one line to the table there.
MarshalResult marshal_command(const godot::Dictionary& dictionary, const MarshalContext& context);

} // namespace citysim::gdext
