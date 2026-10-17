#include "citysim/content/era_def.hpp"

namespace citysim::content {

EraDef parse_era_def(const nlohmann::json& json, ParseContext& context) {
    EraDef era;
    era.id = context.def_id();
    context.warn_unknown_keys(json, {"type", "id", "patch", "name_key", "desc_key", "real_seconds_per_year",
                                     "default_start_year", "order"});
    era.name_key = context.read_string(json, "name_key").value_or("");
    era.desc_key = context.read_string(json, "desc_key").value_or("");
    era.real_seconds_per_year = context.read_u32(json, "real_seconds_per_year").value_or(0);
    era.default_start_year = context.read_i32(json, "default_start_year").value_or(0);
    era.order = context.read_i32(json, "order").value_or(0);
    return era;
}

} // namespace citysim::content
