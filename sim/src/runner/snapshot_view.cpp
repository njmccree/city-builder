#include "citysim/runner/snapshot_view.hpp"

#include <algorithm>

namespace citysim::runner {

void SnapshotView::update(const RenderSnapshot& snapshot) {
    if (!have_current_ || snapshot.tick < current_.tick) {
        previous_ = snapshot;
        current_ = snapshot;
        have_current_ = true;
        return;
    }
    if (snapshot.tick == current_.tick) {
        const double published_at = current_.published_at;
        current_ = snapshot;
        current_.published_at = published_at;
        return;
    }
    previous_ = std::move(current_);
    current_ = snapshot;
}

float SnapshotView::alpha(double now, const time::TimeDefines& defines) const {
    if (!have_current_ || current_.paused || current_.speed >= defines.speed_multipliers.size()) {
        return 1.0F;
    }
    const double ticks_per_second = static_cast<double>(defines.base_tick_rate) *
                                    static_cast<double>(defines.speed_multipliers[current_.speed]);
    if (ticks_per_second <= 0.0) {
        return 1.0F;
    }
    const double interval = 1.0 / ticks_per_second;
    return static_cast<float>(std::clamp((now - current_.published_at) / interval, 0.0, 1.0));
}

} // namespace citysim::runner
