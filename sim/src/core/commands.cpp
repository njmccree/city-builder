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
};
} // namespace

std::string describe(const Command& cmd) {
    return std::visit(Describe{}, cmd);
}

} // namespace citysim::core
