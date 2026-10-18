#include "citysim/content/era_def.hpp"

#include <string>

namespace citysim::content {

EraDef parse_era_def(const nlohmann::json& json, ParseContext& context) {
    EraDef era;
    era.id = context.def_id();
    context.warn_unknown_keys(json, {"type", "id", "patch", "name_key", "desc_key", "real_seconds_per_year",
                                     "default_start_year", "order", "next_era"});
    era.name_key = context.read_string(json, "name_key").value_or("");
    era.desc_key = context.read_string(json, "desc_key").value_or("");
    era.real_seconds_per_year = context.read_u32(json, "real_seconds_per_year").value_or(0);
    era.default_start_year = context.read_i32(json, "default_start_year").value_or(0);
    era.order = context.read_i32(json, "order").value_or(0);
    era.next_era = read_def_ref<EraDef>(context, json, "next_era", /*required=*/false);
    return era;
}

void resolve_era_def(EraDef& era, ParseContext& context) {
    resolve_def_ref(context, era.next_era, "next_era");
}

void validate_era_def(const EraDef& era, const DefRegistry<EraDef>& all, ParseContext& context) {
    if (era.real_seconds_per_year == 0) {
        context.error(context.child_pointer("real_seconds_per_year"), "must be greater than 0");
    }

    // Report a clash on the later id only, so each pair is reported once.
    for (std::size_t i = 0; i < all.size(); ++i) {
        const EraDef& other = all.all()[i];
        if (other.id < era.id && other.order == era.order) {
            context.error(context.child_pointer("order"), "order " + std::to_string(era.order) +
                                                              " is already used by '" + other.id.str() + "'");
            break;
        }
    }

    if (era.next_era.resolved()) {
        const EraDef& next = all.get(era.next_era.handle);
        if (next.order <= era.order) {
            context.error(context.child_pointer("next_era"),
                          "'" + next.id.str() + "' has order " + std::to_string(next.order) +
                              ", which does not come after this era's order " + std::to_string(era.order));
        }
    }
}

} // namespace citysim::content
