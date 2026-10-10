#include "citysim/core/world.hpp"

namespace citysim::core {

World::Entity World::create() {
    return entities_.create();
}

void World::destroy(Entity entity) {
    entities_.destroy(entity);
}

bool World::valid(Entity entity) const {
    return entities_.valid(entity);
}

std::size_t World::entity_count() const {
    // The entity storage's free_list() is the number of live entities; it does not exist until the first
    // create().
    const auto* storage = entities_.storage<Entity>();
    return storage != nullptr ? storage->free_list() : 0;
}

} // namespace citysim::core
