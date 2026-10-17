#pragma once

#include "citysim/content/content_database.hpp"
#include "citysim/content/def_handle.hpp"
#include "citysim/save/archive.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace citysim::content {

// A reference to a content definition is saved as its id, never as the handle's index: indices change when
// mods change. The archive must have been given the ContentDatabase. An invalid handle is saved as "".
// Loading an id the loaded content does not have is a SaveFormatError (the content differs from the save's).
template <class Ar, class T> void serialize(Ar& ar, DefHandle<T>& handle, std::uint32_t /*version*/) {
    const ContentDatabase* content = ar.content();
    if (content == nullptr) {
        throw std::logic_error(
            "saving or loading a definition reference needs an archive with a ContentDatabase");
    }
    const DefRegistry<T>& registry = content->template registry<T>();
    if constexpr (Ar::is_loading) {
        std::string id;
        ar.string(id);
        if (id.empty()) {
            handle = DefHandle<T>{};
            return;
        }
        const auto found = registry.find(std::string_view(id));
        if (!found) {
            ar.fail("the loaded content has no definition '" + id + "' (was a mod removed or changed?)");
        }
        handle = *found;
    } else {
        std::string id = handle.valid() ? registry.id(handle).str() : std::string();
        ar.string(id);
    }
}

} // namespace citysim::content
