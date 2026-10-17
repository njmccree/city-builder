#include "citysim/core/event_bus.hpp"

#include <utility>

namespace citysim::core {

void EventBus::emit(Event event) {
    events_.push_back(std::move(event));
}

void EventBus::clear() {
    events_.clear();
    readable_count_ = 0;
}

} // namespace citysim::core
