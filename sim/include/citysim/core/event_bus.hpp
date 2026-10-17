#pragma once

#include "citysim/core/events.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace citysim::core {

class Simulation;

// Per-tick event buffer (architecture §5.5). Runs on the tick clock.
//
// Visibility rule: an event emitted during phase P can be read from phase P+1 onward, never during P
// itself, so the result cannot depend on the order of systems inside one phase. Every event is
// dropped when the tick ends. Events are kept in emission order, which is deterministic because
// systems run in registration order.
class EventBus {
  public:
    void emit(Event event);

    // Events from earlier phases of this tick, in emission order. The span is invalidated by emit(), so
    // call readable() again after emitting instead of keeping the old span.
    std::span<const Event> readable() const { return std::span<const Event>(events_).first(readable_count_); }

  private:
    friend class Simulation;

    void begin_phase() { readable_count_ = events_.size(); }
    void clear();

    std::vector<Event> events_;
    std::size_t readable_count_ = 0;
};

} // namespace citysim::core
