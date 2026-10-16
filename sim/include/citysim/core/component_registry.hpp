#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
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

// Type-erased description of one registered component type.
struct ComponentInfo {
    std::string name;
    std::type_index type;
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
            std::string(stable_name),
            std::type_index(typeid(T)),
            nullptr,
            nullptr,
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

    // Lookups return nullptr when absent. Pointers are invalidated by the next registration.
    const ComponentInfo* find(std::string_view stable_name) const;
    template <class T> const ComponentInfo* find() const { return find(std::type_index(typeid(T))); }
    const ComponentInfo* find(std::type_index type) const;

    // All entries, sorted by stable name.
    const std::vector<ComponentInfo>& entries() const { return entries_; }
    std::size_t size() const { return entries_.size(); }

  private:
    void add(ComponentInfo info);

    std::vector<ComponentInfo> entries_;
};

} // namespace citysim::core
