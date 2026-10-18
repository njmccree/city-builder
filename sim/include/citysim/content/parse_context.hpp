#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/content_issue.hpp"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace citysim::content {

// How references find their target while the resolve pass runs: by C++ type and id, without depending on the
// database itself.
struct RefLookup {
    std::function<std::optional<std::uint32_t>(std::type_index, const ContentId&)> find_index;
    std::function<std::string(std::type_index)> type_name;
};

// Collects errors and warnings while one definition is parsed, each tagged with the file and JSON pointer
// where the problem is (architecture §8). A parse function reads its fields through the read_* helpers, which
// report missing fields and wrong types, then returns whatever it built; the loader discards the result if
// error_count() grew.
//
// The loader positions the context (file and pointer) before it calls a parse function. The same context is
// used for the later passes (resolving references, validation), positioned at each definition in turn.
class ParseContext {
  public:
    explicit ParseContext(std::vector<ContentIssue>& sink) : sink_(sink) {}

    // --- position, set by the loader ---------------------------------------------------------------------
    void set_file(std::string file) { file_ = std::move(file); }
    void set_mod_root(std::string mod_root) { mod_root_ = std::move(mod_root); }
    // The mods that wrote or changed the definition, in load order. When there is more than one (a mod
    // replaced or patched it), messages say so, because a field in a merged definition does not map to one
    // place in one file.
    void set_layering(std::string mod_id, std::vector<std::string> touched_by) {
        mod_id_ = std::move(mod_id);
        touched_by_ = std::move(touched_by);
    }
    const std::string& mod_id() const { return mod_id_; }
    const std::vector<std::string>& touched_by() const { return touched_by_; }
    void set_pointer(std::string pointer) { pointer_ = std::move(pointer); }
    void set_def_id(ContentId id) { def_id_ = std::move(id); }
    const std::string& file() const { return file_; }
    const std::string& mod_root() const { return mod_root_; }
    const std::string& pointer() const { return pointer_; }
    // The id of the definition being parsed (already validated), for parse functions to store.
    const ContentId& def_id() const { return def_id_; }

    // Present only during the resolve pass.
    void set_ref_lookup(const RefLookup* lookup) { ref_lookup_ = lookup; }
    const RefLookup* ref_lookup() const { return ref_lookup_; }

    // --- reporting ---------------------------------------------------------------------------------------
    void error(std::string pointer, std::string message);
    void warning(std::string pointer, std::string message);
    // Same, at the current pointer.
    void error(std::string message) { error(pointer_, std::move(message)); }
    std::size_t error_count() const { return error_count_; }

    // --- reading fields of `object`, which is located at the current pointer ----------------------------
    // Each returns nullopt after reporting if the field is missing (and required) or has the wrong type.
    std::optional<std::string> read_string(const nlohmann::json& object, std::string_view key,
                                           bool required = true);
    std::optional<std::uint32_t> read_u32(const nlohmann::json& object, std::string_view key,
                                          bool required = true);
    std::optional<std::int32_t> read_i32(const nlohmann::json& object, std::string_view key,
                                         bool required = true);
    std::optional<double> read_number(const nlohmann::json& object, std::string_view key,
                                      bool required = true);
    std::optional<bool> read_bool(const nlohmann::json& object, std::string_view key, bool required = true);

    // Warns about every key of `object` that is not in `known` (typos otherwise pass silently).
    void warn_unknown_keys(const nlohmann::json& object, std::initializer_list<std::string_view> known);

    // The pointer of `key` inside the current object, with RFC 6901 escaping.
    std::string child_pointer(std::string_view key) const;
    static std::string escape_pointer_token(std::string_view token);

  private:
    std::string layered_suffix() const;
    const nlohmann::json* find_field(const nlohmann::json& object, std::string_view key, bool required);

    std::vector<ContentIssue>& sink_;
    std::string file_;
    std::string mod_root_;
    std::string mod_id_;
    std::vector<std::string> touched_by_;
    std::string pointer_;
    const RefLookup* ref_lookup_ = nullptr;
    ContentId def_id_;
    std::size_t error_count_ = 0;
};

} // namespace citysim::content
