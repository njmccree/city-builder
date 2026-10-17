#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/content_issue.hpp"

#include <cstdint>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace citysim::content {

// Collects errors and warnings while one definition is parsed, each tagged with the file and JSON pointer
// where the problem is (architecture §8). A parse function reads its fields through the read_* helpers, which
// report missing fields and wrong types, then returns whatever it built; the loader discards the result if
// error_count() grew.
//
// The loader positions the context (file and pointer) before it calls a parse function.
class ParseContext {
  public:
    explicit ParseContext(std::vector<ContentIssue>& sink) : sink_(sink) {}

    // --- position, set by the loader ---------------------------------------------------------------------
    void set_file(std::string file) { file_ = std::move(file); }
    void set_pointer(std::string pointer) { pointer_ = std::move(pointer); }
    void set_def_id(ContentId id) { def_id_ = std::move(id); }
    const std::string& file() const { return file_; }
    const std::string& pointer() const { return pointer_; }
    // The id of the definition being parsed (already validated), for parse functions to store.
    const ContentId& def_id() const { return def_id_; }

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
    const nlohmann::json* find_field(const nlohmann::json& object, std::string_view key, bool required);

    std::vector<ContentIssue>& sink_;
    std::string file_;
    std::string pointer_;
    ContentId def_id_;
    std::size_t error_count_ = 0;
};

} // namespace citysim::content
