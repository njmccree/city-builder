#pragma once

#include "citysim/core/component_registry.hpp"

#include <cstddef>
#include <entt/entt.hpp>
#include <utility>

namespace citysim::core {

// Owns all simulation state: the EnTT registry plus singleton context state (architecture §5.1).
// Not tied to a clock; systems mutate it during their phases (§6).
class World {
  public:
    using Entity = entt::entity;

    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;
    ~World() = default;

    Entity create();
    void destroy(Entity entity);
    bool valid(Entity entity) const;
    std::size_t entity_count() const;

    // Returns void for empty (tag) types, as EnTT does.
    template <class T, class... Args> decltype(auto) emplace(Entity entity, Args&&... args) {
        return entities_.emplace<T>(entity, std::forward<Args>(args)...);
    }
    template <class T> T& get(Entity entity) { return entities_.get<T>(entity); }
    template <class T> const T& get(Entity entity) const { return entities_.get<T>(entity); }
    template <class T> T* try_get(Entity entity) { return entities_.try_get<T>(entity); }
    template <class T> const T* try_get(Entity entity) const { return entities_.try_get<T>(entity); }
    template <class T> std::size_t remove(Entity entity) { return entities_.remove<T>(entity); }
    template <class... Ts> auto view() { return entities_.view<Ts...>(); }
    template <class... Ts> auto view() const { return entities_.view<Ts...>(); }

    // Singleton state (clock, calendar, map, ...).
    entt::registry::context& ctx() { return entities_.ctx(); }
    const entt::registry::context& ctx() const { return entities_.ctx(); }

    ComponentRegistry& registry() { return components_; }
    const ComponentRegistry& registry() const { return components_; }

  private:
    entt::registry entities_;
    ComponentRegistry components_;
};

} // namespace citysim::core
