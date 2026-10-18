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
    std::string operator()(const SaveGame& cmd) const { return "SaveGame path=" + cmd.path; }
    std::string operator()(const LoadGame& cmd) const { return "LoadGame path=" + cmd.path; }
    std::string operator()(const debug::SpawnDebugAgents& cmd) const {
        return "SpawnDebugAgents count=" + std::to_string(cmd.count);
    }
    std::string operator()(const debug::ClearDebugAgents&) const { return "ClearDebugAgents"; }
    std::string operator()(const RequestStateHash&) const { return "RequestStateHash"; }
    std::string operator()(const time::SetTimeScale& cmd) const {
        return "SetTimeScale movement_scale=" + std::to_string(cmd.movement_scale) +
               " day_length_ticks=" + std::to_string(cmd.day_length_ticks);
    }
};
} // namespace

std::string describe(const Command& cmd) {
    return std::visit(Describe{}, cmd);
}

} // namespace citysim::core
