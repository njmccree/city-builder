#include "citysim/save/entity_remap.hpp"

#include <algorithm>

namespace citysim::save {

EntityRemap EntityRemap::for_saving(std::vector<entt::entity> entities) {
    EntityRemap remap;
    remap.entities_ = std::move(entities);
    return remap;
}

bool EntityRemap::index_of(entt::entity entity, std::uint64_t& index) const {
    // Sorted by entity index; compare the same way the list was sorted.
    const auto it =
        std::lower_bound(entities_.begin(), entities_.end(), entity, [](entt::entity a, entt::entity b) {
            return entt::to_entity(a) < entt::to_entity(b);
        });
    if (it == entities_.end() || *it != entity) {
        return false;
    }
    index = static_cast<std::uint64_t>(it - entities_.begin());
    return true;
}

bool EntityRemap::entity_at(std::uint64_t index, entt::entity& entity) const {
    if (index >= entities_.size()) {
        return false;
    }
    entity = entities_[static_cast<std::size_t>(index)];
    return true;
}

} // namespace citysim::save
