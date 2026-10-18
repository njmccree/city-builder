#pragma once

#include "citysim/core/commands.hpp"
#include "citysim/core/tick_context.hpp"

#include <array>
#include <functional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace citysim::core {

// Routes a command to the handler registered for its type. Runs inside the ApplyCommands phase, so it
// is on the tick clock.
class CommandDispatcher {
  public:
    template <class T> using Handler = std::function<void(TickContext&, const T&)>;

    // One handler per command type. Throws std::logic_error if the type already has one.
    template <class T> void register_handler(Handler<T> handler) {
        const std::size_t index = Command(std::in_place_type<T>).index();
        if (handlers_[index]) {
            throw std::logic_error("a handler is already registered for this command type");
        }
        handlers_[index] = [handler = std::move(handler)](TickContext& context, const Command& cmd) {
            handler(context, std::get<T>(cmd));
        };
    }

    // Returns false, without throwing, when no handler is registered for the command's type.
    bool dispatch(TickContext& context, const Command& cmd) const;

  private:
    std::array<std::function<void(TickContext&, const Command&)>, std::variant_size_v<Command>> handlers_;
};

} // namespace citysim::core
