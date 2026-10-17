#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/def_ref.hpp"
#include "citysim/content/def_registry.hpp"
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
    DefRef<EraDef> next_era;                 // optional: the era that follows this one
};

EraDef parse_era_def(const nlohmann::json& json, ParseContext& context);
void resolve_era_def(EraDef& era, ParseContext& context);
// Needs real_seconds_per_year > 0, `order` unique across eras, and next_era later in `order` than this era.
void validate_era_def(const EraDef& era, const DefRegistry<EraDef>& all, ParseContext& context);

} // namespace citysim::content
