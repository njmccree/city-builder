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

class StateHasher;

} // namespace citysim::core

namespace citysim::save {
class BinaryWriter;
class BinaryReader;
} // namespace citysim::save

namespace citysim::core {

// Per-type hooks (architecture §4, §10). Set `write` and `read` together with save::bind_serializer();
// `write` does not modify the value (it takes a non-const reference only so one function serves both
// directions). Types with no data (tags) need neither. `version` is the layout version written by this build;
// bump it whenever the serialized layout changes.
template <class T> struct Hooks {
    void (*hash)(StateHasher&, const T&) = nullptr;
    void (*write)(save::BinaryWriter&, T&, std::uint32_t version) = nullptr;
    void (*read)(save::BinaryReader&, T&, std::uint32_t version) = nullptr;
    std::uint32_t version = 1;
};

// One entity's component, as seen through the type-erased registry.
struct ComponentRef {
    entt::entity entity;
    const void* data;
};

// Type-erased description of one registered component type.
struct ComponentInfo {
    std::string name;
    std::type_index type = std::type_index(typeid(void));
    // Appends every (entity, component) pair of this type, in storage order (not sorted).
    std::function<void(const entt::registry&, std::vector<ComponentRef>&)> collect;
    std::function<void(StateHasher&, const void*)> hash;
    // Saving: writes one component at the current `version`. Loading: `read` fills a component created by
    // `create` from data written at `version` (older or equal to the current one).
    std::function<void(save::BinaryWriter&, const void*)> write;
    std::function<void(save::BinaryReader&, void*, std::uint32_t)> read;
    std::uint32_t version = 1;
    // Adds a default-constructed component to `entity` and returns it; nullptr if the type cannot be
    // default-constructed (then it cannot be loaded).
    std::function<void*(entt::registry&, entt::entity)> create;
    bool serializable() const { return write && read; }
};

// Type-erased description of one registered singleton (a ctx() variable).
struct SingletonInfo {
    std::string name;
    std::type_index type = std::type_index(typeid(void));
    // Returns the singleton, or nullptr if it has not been emplaced in the context.
    std::function<const void*(const entt::registry::context&)> find;
    std::function<void(StateHasher&, const void*)> hash;
    std::function<void(save::BinaryWriter&, const void*)> write;
    std::function<void(save::BinaryReader&, void*, std::uint32_t)> read;
    std::uint32_t version = 1;
    // Returns the singleton, emplacing a default one first if there is none; nullptr if the type cannot be
    // default-constructed.
    std::function<void*(entt::registry::context&)> get_or_create;
    bool serializable() const { return write && read; }
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
        ComponentInfo info;
        info.name = std::string(stable_name);
        info.type = std::type_index(typeid(T));
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
        set_hooks<T>(info, hooks);
        if constexpr (std::is_default_constructible_v<T>) {
            info.create = [](entt::registry& registry, entt::entity entity) -> void* {
                if constexpr (std::is_empty_v<T>) {
                    static T k_empty{};
                    registry.emplace<T>(entity);
                    return &k_empty; // nothing to fill in
                } else {
                    return &registry.emplace<T>(entity);
                }
            };
        }
        add(std::move(info));
    }

    // Same rules for ctx() singletons. Component and singleton names are separate namespaces.
    template <class T> void register_singleton(std::string_view stable_name, Hooks<T> hooks) {
        SingletonInfo info;
        info.name = std::string(stable_name);
        info.type = std::type_index(typeid(T));
        info.find = [](const entt::registry::context& ctx) -> const void* { return ctx.find<T>(); };
        set_hooks<T>(info, hooks);
        if constexpr (std::is_default_constructible_v<T>) {
            info.get_or_create = [](entt::registry::context& ctx) -> void* {
                if (T* existing = ctx.find<T>()) {
                    return existing;
                }
                return &ctx.emplace<T>();
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
    // Installs the type-erased hash and save hooks, shared by components and singletons.
    template <class T, class Info> static void set_hooks(Info& info, const Hooks<T>& hooks) {
        info.version = hooks.version;
        if (hooks.hash != nullptr) {
            auto fn = hooks.hash;
            info.hash = [fn](StateHasher& hasher, const void* value) {
                fn(hasher, *static_cast<const T*>(value));
            };
        }
        if (hooks.write != nullptr && hooks.read != nullptr) {
            auto write = hooks.write;
            auto read = hooks.read;
            // The writer does not modify the value (see Hooks), so dropping const here is safe.
            info.write = [write, version = hooks.version](save::BinaryWriter& ar, const void* value) {
                write(ar, *const_cast<T*>(static_cast<const T*>(value)), version);
            };
            info.read = [read](save::BinaryReader& ar, void* value, std::uint32_t version) {
                read(ar, *static_cast<T*>(value), version);
            };
        } else if constexpr (std::is_empty_v<T>) {
            // A type with no data has nothing to save, so it needs no hooks.
            info.write = [](save::BinaryWriter&, const void*) {};
            info.read = [](save::BinaryReader&, void*, std::uint32_t) {};
        }
    }

    void add(ComponentInfo info);
    void add_singleton(SingletonInfo info);

    std::vector<ComponentInfo> entries_;
    std::vector<SingletonInfo> singletons_;
};

} // namespace citysim::core
