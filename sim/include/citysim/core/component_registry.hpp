#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace citysim::core {

// Implemented by later tasks (state hashing §4, save/load archive §10).
class StateHasher;
class Archive;

// Per-type hooks. Both may be null until the corresponding system exists.
template <class T> struct Hooks {
    void (*hash)(StateHasher&, const T&) = nullptr;
    void (*serialize)(Archive&, T&, std::uint32_t version) = nullptr;
};

// One entity's component, as seen through the type-erased registry.
struct ComponentRef {
    entt::entity entity;
    const void* data;
};

// Type-erased description of one registered component type.
struct ComponentInfo {
    std::string name;
    std::type_index type;
    // Appends every (entity, component) pair of this type, in storage order (not sorted).
    std::function<void(const entt::registry&, std::vector<ComponentRef>&)> collect;
    std::function<void(StateHasher&, const void*)> hash;
    std::function<void(Archive&, void*, std::uint32_t)> serialize;
};

// Type-erased description of one registered singleton (a ctx() variable).
struct SingletonInfo {
    std::string name;
    std::type_index type;
    // Returns the singleton, or nullptr if it has not been emplaced in the context.
    std::function<const void*(const entt::registry::context&)> find;
    std::function<void(StateHasher&, const void*)> hash;
    std::function<void(Archive&, void*, std::uint32_t)> serialize;
};

// Registry of every stateful component type (architecture §5.1).
// Not tied to a clock: it is configuration, populated before the simulation runs.
//
// Entries are always kept sorted by stable name so hashing and saving never depend on the order in
// which types happened to be registered (determinism rule 3 / §5.1).
class ComponentRegistry {
  public:
    // Throws std::logic_error if the name or the type is already registered.
    template <class T> void register_type(std::string_view stable_name, Hooks<T> hooks) {
        ComponentInfo info{
            std::string(stable_name), std::type_index(typeid(T)), nullptr, nullptr, nullptr,
        };
        info.collect = [](const entt::registry& registry, std::vector<ComponentRef>& out) {
            auto view = registry.view<const T>();
            if constexpr (std::is_empty_v<T>) {
                static const T k_empty{}; // empty components have no storage to point into
                for (const auto entity : view) {
                    out.push_back({entity, &k_empty});
                }
            } else {
                for (const auto entity : view) {
                    out.push_back({entity, &view.template get<const T>(entity)});
                }
            }
        };
        if (hooks.hash != nullptr) {
            auto fn = hooks.hash;
            info.hash = [fn](StateHasher& hasher, const void* value) {
                fn(hasher, *static_cast<const T*>(value));
            };
        }
        if (hooks.serialize != nullptr) {
            auto fn = hooks.serialize;
            info.serialize = [fn](Archive& ar, void* value, std::uint32_t version) {
                fn(ar, *static_cast<T*>(value), version);
            };
        }
        add(std::move(info));
    }

    // Same rules for ctx() singletons. Component and singleton names are separate namespaces.
    template <class T> void register_singleton(std::string_view stable_name, Hooks<T> hooks) {
        SingletonInfo info{
            std::string(stable_name), std::type_index(typeid(T)), nullptr, nullptr, nullptr,
        };
        info.find = [](const entt::registry::context& ctx) -> const void* { return ctx.find<T>(); };
        if (hooks.hash != nullptr) {
            auto fn = hooks.hash;
            info.hash = [fn](StateHasher& hasher, const void* value) {
                fn(hasher, *static_cast<const T*>(value));
            };
        }
        if (hooks.serialize != nullptr) {
            auto fn = hooks.serialize;
            info.serialize = [fn](Archive& ar, void* value, std::uint32_t version) {
                fn(ar, *static_cast<T*>(value), version);
            };
        }
        add_singleton(std::move(info));
    }

    // Lookups return nullptr when absent. Pointers are invalidated by the next registration.
    const ComponentInfo* find(std::string_view stable_name) const;
    template <class T> const ComponentInfo* find() const { return find(std::type_index(typeid(T))); }
    const ComponentInfo* find(std::type_index type) const;

    const SingletonInfo* find_singleton(std::string_view stable_name) const;

    // All entries, sorted by stable name.
    const std::vector<ComponentInfo>& entries() const { return entries_; }
    const std::vector<SingletonInfo>& singletons() const { return singletons_; }
    std::size_t size() const { return entries_.size(); }

  private:
    void add(ComponentInfo info);
    void add_singleton(SingletonInfo info);

    std::vector<ComponentInfo> entries_;
    std::vector<SingletonInfo> singletons_;
};

} // namespace citysim::core
