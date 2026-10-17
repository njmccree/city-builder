#pragma once

#include <string>
#include <variant>

namespace citysim::time {

// What the calendar scheduler can be asked to do at a date. Plain data, no std::function, so a pending action
// can be hashed and saved. Later tasks add their own action structs here, the same way commands are listed in
// citysim/core/commands.hpp, and append them to the variant (never reorder: saves record the index).
struct DebugAction {
    std::string tag;
};

using ScheduledAction = std::variant<DebugAction>;

} // namespace citysim::time
