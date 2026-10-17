#include "citysim/core/commands.hpp"

#include <variant>

namespace citysim::core {

namespace {
// An overload per command type: a missing one is a compile error, which is the point.
struct Describe {
    std::string operator()(const NoOp&) const { return "NoOp"; }
    std::string operator()(const DebugSetComponentValue& cmd) const {
        return "DebugSetComponentValue entity=" + std::to_string(cmd.entity) +
               " value=" + std::to_string(cmd.value);
    }
    std::string operator()(const time::SetSpeed& cmd) const {
        return "SetSpeed speed=" + std::to_string(cmd.speed);
    }
    std::string operator()(const time::TogglePause&) const { return "TogglePause"; }
    std::string operator()(const time::SetEra& cmd) const { return "SetEra era=" + cmd.era_id; }
};
} // namespace

std::string describe(const Command& cmd) {
    return std::visit(Describe{}, cmd);
}

} // namespace citysim::core
