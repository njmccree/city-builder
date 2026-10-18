#include "citysim/core/world.hpp"

#include "citysim/core/state_hasher.hpp"

#include <algorithm>
#include <vector>

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

std::vector<World::Entity> World::entities() const {
    std::vector<Entity> live;
    if (const auto* storage = entities_.storage<Entity>()) {
        for (const auto [entity] : storage->each()) {
            live.push_back(entity);
        }
    }
    std::sort(live.begin(), live.end(),
              [](Entity a, Entity b) { return entt::to_entity(a) < entt::to_entity(b); });
    return live;
}

std::uint64_t World::state_hash() const {
    // Everything is visited in an order that depends only on state, never on creation or registration
    // order (§4 rules 3 and 8): entities by ascending index, components and singletons by stable name.
    // Counts are hashed before each list so adjacent lists cannot blur into each other. Entity values
    // include the version, so a recycled id hashes differently from the original.
    const auto by_index = [](Entity a, Entity b) { return entt::to_entity(a) < entt::to_entity(b); };
    StateHasher hasher;

    const std::vector<Entity> live = entities();
    hasher.add("entities");
    hasher.add_pod(static_cast<std::uint64_t>(live.size()));
    for (const Entity entity : live) {
        hasher.add_pod(entt::to_integral(entity));
    }

    std::vector<ComponentRef> refs;
    for (const ComponentInfo& info : components_.entries()) {
        refs.clear();
        info.collect(entities_, refs);
        std::sort(refs.begin(), refs.end(),
                  [&](const ComponentRef& a, const ComponentRef& b) { return by_index(a.entity, b.entity); });
        hasher.add("component");
        hasher.add(info.name);
        hasher.add_pod(static_cast<std::uint64_t>(refs.size()));
        for (const ComponentRef& ref : refs) {
            hasher.add_pod(entt::to_integral(ref.entity));
            if (info.hash) {
                info.hash(hasher, ref.data);
            }
        }
    }

    for (const SingletonInfo& info : components_.singletons()) {
        const void* value = info.find(entities_.ctx());
        hasher.add("singleton");
        hasher.add(info.name);
        hasher.add_pod(static_cast<std::uint8_t>(value != nullptr ? 1 : 0));
        if (value != nullptr && info.hash) {
            info.hash(hasher, value);
        }
    }

    return hasher.digest();
}

} // namespace citysim::core
