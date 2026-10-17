#include "citysim/core/component_registry.hpp"

#include <algorithm>
#include <stdexcept>

namespace citysim::core {

namespace {

bool name_less(const auto& info, std::string_view name) {
    return info.name < name;
}

// Inserts keeping `entries` sorted by name; throws on a duplicate name or type.
template <class Info> void insert_sorted(std::vector<Info>& entries, Info info, const char* what) {
    const auto pos = std::lower_bound(entries.begin(), entries.end(), std::string_view(info.name),
                                      [](const Info& a, std::string_view n) { return name_less(a, n); });
    if (pos != entries.end() && pos->name == info.name) {
        throw std::logic_error(std::string(what) + " name already registered: " + info.name);
    }
    const auto same_type = std::find_if(entries.begin(), entries.end(),
                                        [&](const Info& other) { return other.type == info.type; });
    if (same_type != entries.end()) {
        throw std::logic_error(std::string(what) + " type already registered: " + info.name);
    }
    entries.insert(pos, std::move(info));
}

} // namespace

void ComponentRegistry::add(ComponentInfo info) {
    insert_sorted(entries_, std::move(info), "component");
}

void ComponentRegistry::add_singleton(SingletonInfo info) {
    insert_sorted(singletons_, std::move(info), "singleton");
}

const SingletonInfo* ComponentRegistry::find_singleton(std::string_view stable_name) const {
    const auto pos =
        std::lower_bound(singletons_.begin(), singletons_.end(), stable_name,
                         [](const SingletonInfo& a, std::string_view n) { return name_less(a, n); });
    return (pos != singletons_.end() && pos->name == stable_name) ? &*pos : nullptr;
}

const ComponentInfo* ComponentRegistry::find(std::string_view stable_name) const {
    const auto pos =
        std::lower_bound(entries_.begin(), entries_.end(), stable_name,
                         [](const ComponentInfo& a, std::string_view n) { return name_less(a, n); });
    return (pos != entries_.end() && pos->name == stable_name) ? &*pos : nullptr;
}

const ComponentInfo* ComponentRegistry::find(std::type_index type) const {
    // Lookup only; the result does not depend on iteration order.
    const auto pos = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const ComponentInfo& info) { return info.type == type; });
    return pos != entries_.end() ? &*pos : nullptr;
}

} // namespace citysim::core
