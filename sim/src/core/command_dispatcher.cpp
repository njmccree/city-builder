#include "citysim/core/command_dispatcher.hpp"

namespace citysim::core {

bool CommandDispatcher::dispatch(TickContext& context, const Command& cmd) const {
    const auto& handler = handlers_[cmd.index()];
    if (!handler) {
        return false;
    }
    handler(context, cmd);
    return true;
}

} // namespace citysim::core
