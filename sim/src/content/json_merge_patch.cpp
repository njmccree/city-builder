#include "citysim/content/json_merge_patch.hpp"

namespace citysim::content {

nlohmann::json merge_patch(nlohmann::json target, const nlohmann::json& patch) {
    if (!patch.is_object()) {
        return patch;
    }
    if (!target.is_object()) {
        target = nlohmann::json::object();
    }
    for (const auto& member : patch.items()) { // key order, so the result never depends on insertion order
        if (member.value().is_null()) {
            target.erase(member.key());
        } else {
            const auto existing = target.find(member.key());
            nlohmann::json current = existing == target.end() ? nlohmann::json() : *existing;
            target[member.key()] = merge_patch(std::move(current), member.value());
        }
    }
    return target;
}

} // namespace citysim::content
