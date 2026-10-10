#include "citysim/core/component_registry.hpp"

#include <algorithm>
#include <stdexcept>

namespace citysim::core {

namespace {
bool name_less(const ComponentInfo& info, std::string_view name) {
    return info.name < name;
}
} // namespace

void ComponentRegistry::add(ComponentInfo info) {
    const auto pos =
        std::lower_bound(entries_.begin(), entries_.end(), std::string_view(info.name), name_less);
    if (pos != entries_.end() && pos->name == info.name) {
        throw std::logic_error("component name already registered: " + info.name);
    }
    if (find(info.type) != nullptr) {
        throw std::logic_error("component type already registered: " + info.name);
    }
    entries_.insert(pos, std::move(info));
}

const ComponentInfo* ComponentRegistry::find(std::string_view stable_name) const {
    const auto pos = std::lower_bound(entries_.begin(), entries_.end(), stable_name, name_less);
    return (pos != entries_.end() && pos->name == stable_name) ? &*pos : nullptr;
}

const ComponentInfo* ComponentRegistry::find(std::type_index type) const {
    // Lookup only; the result does not depend on iteration order.
    const auto pos = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const ComponentInfo& info) { return info.type == type; });
    return pos != entries_.end() ? &*pos : nullptr;
}

} // namespace citysim::core
