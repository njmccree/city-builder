#pragma once

#include "citysim/core/isystem.hpp"

namespace citysim::time {

// Advances the daily cycle by one tick. Registered in the PreUpdate phase so everything later in the tick
// sees the new time of day. Tick clock.
class ClockSystem final : public core::ISystem {
  public:
    std::string_view name() const override { return "time.clock"; }
    void update(core::TickContext& context) override;
};

} // namespace citysim::time
