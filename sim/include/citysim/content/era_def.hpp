#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/parse_context.hpp"

#include <cstdint>
#include <string>

namespace citysim::content {

// One era of the game (Ancient, Colonial, ...). The ids and numbers are data in game/content, not code.
struct EraDef {
    ContentId id;
    std::string name_key; // localization keys, never player-facing text (architecture §9)
    std::string desc_key;
    std::uint32_t real_seconds_per_year = 0; // at 1x: sets how fast the calendar runs (architecture §6)
    std::int32_t default_start_year = 0;     // astronomical numbering: year 0 is 1 BCE
    std::int32_t order = 0;                  // position in the era sequence
};

EraDef parse_era_def(const nlohmann::json& json, ParseContext& context);

} // namespace citysim::content
