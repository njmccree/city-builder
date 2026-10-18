#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace citysim::core {

// A message from the simulation to the UI about the outcome of a command (a city could not be founded, and
// why). Not game state: it is not hashed or saved, and it never feeds back into the simulation.
//   tag  what happened, a short machine name such as "city_found_failed"
//   key  a localization key for the text the player sees (architecture §9), or empty
//   id   the thing it is about (a city id), or 0
struct UiNotice {
    std::string tag;
    std::string key;
    std::uint32_t id = 0;
};

// Collects the notices handlers and systems write during a tick. Simulation puts one in World::ctx(); the
// runner empties it after every tick and every between-ticks command batch and hands each notice to the UI as
// a result event that is kept until read (UiEventKind::Notice), so a notice cannot be missed when several
// ticks pass between two frames. Tied to the tick clock only in that it is written during ticks.
class UiOutbox {
  public:
    void push(UiNotice notice) { notices_.push_back(std::move(notice)); }
    // Everything pushed since the last call, oldest first.
    std::vector<UiNotice> take() { return std::exchange(notices_, {}); }
    bool empty() const { return notices_.empty(); }

  private:
    std::vector<UiNotice> notices_;
};

} // namespace citysim::core
