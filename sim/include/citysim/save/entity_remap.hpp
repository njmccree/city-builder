#pragma once

#include <cstddef>
#include <cstdint>
#include <entt/entt.hpp>
#include <vector>

namespace citysim::save {

// Maps entities to dense save indices and back (architecture §10). A save never contains raw entity ids:
// they depend on how the registry happened to allocate and recycle them. Entities are written as their
// position (0..n-1) among the live entities in ascending order, and loading creates new entities and looks
// them up by that position.
//
// Not tied to a clock; built per save or load.
class EntityRemap {
  public:
    // For saving: `entities` must be the live entities in ascending order of entity index.
    static EntityRemap for_saving(std::vector<entt::entity> entities);

    // For loading: starts empty; add() each entity as it is created, in save order.
    EntityRemap() = default;
    void add(entt::entity entity) { entities_.push_back(entity); }

    std::size_t size() const { return entities_.size(); }

    // Save side. Returns false if `entity` is not one of the entities (dead, or from another registry).
    bool index_of(entt::entity entity, std::uint64_t& index) const;
    // Load side. Returns false if `index` is out of range.
    bool entity_at(std::uint64_t index, entt::entity& entity) const;

  private:
    std::vector<entt::entity> entities_;
};

} // namespace citysim::save
