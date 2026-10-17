#pragma once

#include "citysim/runner/render_snapshot.hpp"
#include "citysim/time/time_defines.hpp"

namespace citysim::runner {

// What the front end keeps between frames (architecture §7.3): the previous and the current RenderSnapshot,
// and from them the interpolation alpha a renderer uses to draw smoothly between ticks. Plain data, no
// threads: the main thread owns it and calls update() once per frame with the newest snapshot. Not tied to a
// clock itself; `now` comes from the same clock that stamps `published_at` (the runner's IClock).
class SnapshotView {
  public:
    // Takes the newest snapshot. A new tick moves current to previous; the same tick again (an extra publish
    // for a save result, say) refreshes the data but keeps the time the tick was first published at. A tick
    // that is lower than before, or reset(), means the game was replaced, so there is nothing to
    // interpolate from.
    void update(const RenderSnapshot& snapshot);

    // Forget the history; the next update() starts afresh. Call after a load.
    void reset() { have_current_ = false; }

    bool has_snapshot() const { return have_current_; }
    const RenderSnapshot& current() const { return current_; }
    const RenderSnapshot& previous() const { return previous_; }

    // clamp((now - current.published_at) / tick_interval, 0, 1): how far between the previous and current
    // state it is time to draw. 1 while paused or before there is a snapshot. The tick interval is 1 /
    // (base_tick_rate x the multiplier of the current speed).
    float alpha(double now, const time::TimeDefines& defines) const;

  private:
    RenderSnapshot previous_;
    RenderSnapshot current_;
    bool have_current_ = false;
};

} // namespace citysim::runner
