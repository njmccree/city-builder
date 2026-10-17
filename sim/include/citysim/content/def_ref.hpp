#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/def_handle.hpp"
#include "citysim/content/parse_context.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <typeindex>

namespace citysim::content {

// A reference from one definition to another (architecture §8). In the file it is the target's full id. The
// parse step stores the id; the resolve pass, after every file has loaded, fills in the handle or reports the
// id that does not exist.
template <class T> struct DefRef {
    ContentId id;
    DefHandle<T> handle;

    bool has_value() const { return !id.empty(); } // a field that is optional and was left out has none
    bool resolved() const { return handle.valid(); }
};

// Reads a reference field. Returns an unset DefRef if an optional field is absent or the id is malformed (the
// latter is reported). Use for fields of the definition at the context's current pointer.
template <class T>
DefRef<T> read_def_ref(ParseContext& context, const nlohmann::json& object, std::string_view key,
                       bool required = true) {
    DefRef<T> ref;
    const std::optional<std::string> text = context.read_string(object, key, required);
    if (!text) {
        return ref;
    }
    std::string reason;
    if (auto id = ContentId::parse(*text, &reason)) {
        ref.id = std::move(*id);
    } else {
        context.error(context.child_pointer(key), "invalid id '" + *text + "': " + reason);
    }
    return ref;
}

// Resolve-pass helper: points `ref` at its target, or reports "unresolved reference" at `key` and leaves it
// unresolved. Does nothing for a reference that is not set. Returns true if the reference is usable (or
// unset).
template <class T> bool resolve_def_ref(ParseContext& context, DefRef<T>& ref, std::string_view key) {
    if (!ref.has_value()) {
        return true;
    }
    const RefLookup* lookup = context.ref_lookup();
    const std::optional<std::uint32_t> index =
        lookup != nullptr ? lookup->find_index(std::type_index(typeid(T)), ref.id) : std::nullopt;
    if (!index) {
        const std::string type_name =
            lookup != nullptr ? lookup->type_name(std::type_index(typeid(T))) : "definition";
        context.error(context.child_pointer(key),
                      "unresolved reference: no " + type_name + " has the id '" + ref.id.str() + "'");
        return false;
    }
    ref.handle = DefHandle<T>{*index};
    return true;
}

} // namespace citysim::content
