#include "citysim/content/parse_context.hpp"

#include <algorithm>
#include <limits>

namespace citysim::content {

std::string ParseContext::escape_pointer_token(std::string_view token) {
    std::string out;
    for (const char c : token) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out += c;
        }
    }
    return out;
}

std::string ParseContext::child_pointer(std::string_view key) const {
    return pointer_ + "/" + escape_pointer_token(key);
}

void ParseContext::error(std::string pointer, std::string message) {
    ++error_count_;
    sink_.push_back(ContentIssue{Severity::Error, file_, std::move(pointer), std::move(message), mod_root_});
}

void ParseContext::warning(std::string pointer, std::string message) {
    sink_.push_back(
        ContentIssue{Severity::Warning, file_, std::move(pointer), std::move(message), mod_root_});
}

const nlohmann::json* ParseContext::find_field(const nlohmann::json& object, std::string_view key,
                                               bool required) {
    if (!object.is_object()) {
        error(pointer_, "expected an object");
        return nullptr;
    }
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        if (required) {
            error(pointer_, "missing required field '" + std::string(key) + "'");
        }
        return nullptr;
    }
    return &*it;
}

std::optional<std::string> ParseContext::read_string(const nlohmann::json& object, std::string_view key,
                                                     bool required) {
    const nlohmann::json* field = find_field(object, key, required);
    if (field == nullptr) {
        return std::nullopt;
    }
    if (!field->is_string()) {
        error(child_pointer(key), "expected a string");
        return std::nullopt;
    }
    return field->get<std::string>();
}

std::optional<std::uint32_t> ParseContext::read_u32(const nlohmann::json& object, std::string_view key,
                                                    bool required) {
    const nlohmann::json* field = find_field(object, key, required);
    if (field == nullptr) {
        return std::nullopt;
    }
    if (!field->is_number_integer() ||
        (field->is_number_integer() && !field->is_number_unsigned() && field->get<std::int64_t>() < 0)) {
        error(child_pointer(key), "expected a non-negative whole number");
        return std::nullopt;
    }
    const std::uint64_t value = field->get<std::uint64_t>();
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        error(child_pointer(key), "number is too large");
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

std::optional<std::uint64_t> ParseContext::read_u64(const nlohmann::json& object, std::string_view key,
                                                    bool required) {
    const nlohmann::json* field = find_field(object, key, required);
    if (field == nullptr) {
        return std::nullopt;
    }
    if (!field->is_number_integer() || (!field->is_number_unsigned() && field->get<std::int64_t>() < 0)) {
        error(child_pointer(key), "expected a non-negative whole number");
        return std::nullopt;
    }
    return field->get<std::uint64_t>();
}

std::optional<std::int32_t> ParseContext::read_i32(const nlohmann::json& object, std::string_view key,
                                                   bool required) {
    const nlohmann::json* field = find_field(object, key, required);
    if (field == nullptr) {
        return std::nullopt;
    }
    if (!field->is_number_integer()) {
        error(child_pointer(key), "expected a whole number");
        return std::nullopt;
    }
    if (field->is_number_unsigned()
            ? field->get<std::uint64_t>() >
                  static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
            : (field->get<std::int64_t>() < std::numeric_limits<std::int32_t>::min())) {
        error(child_pointer(key), "number is out of range");
        return std::nullopt;
    }
    return static_cast<std::int32_t>(field->get<std::int64_t>());
}

std::optional<double> ParseContext::read_number(const nlohmann::json& object, std::string_view key,
                                                bool required) {
    const nlohmann::json* field = find_field(object, key, required);
    if (field == nullptr) {
        return std::nullopt;
    }
    if (!field->is_number()) {
        error(child_pointer(key), "expected a number");
        return std::nullopt;
    }
    return field->get<double>();
}

std::optional<bool> ParseContext::read_bool(const nlohmann::json& object, std::string_view key,
                                            bool required) {
    const nlohmann::json* field = find_field(object, key, required);
    if (field == nullptr) {
        return std::nullopt;
    }
    if (!field->is_boolean()) {
        error(child_pointer(key), "expected true or false");
        return std::nullopt;
    }
    return field->get<bool>();
}

void ParseContext::warn_unknown_keys(const nlohmann::json& object,
                                     std::initializer_list<std::string_view> known) {
    if (!object.is_object()) {
        return;
    }
    for (const auto& item : object.items()) { // nlohmann objects iterate in key order: deterministic output
        const bool is_known =
            std::any_of(known.begin(), known.end(), [&](std::string_view k) { return k == item.key(); });
        if (!is_known) {
            warning(child_pointer(item.key()), "unknown field '" + item.key() + "' is ignored (typo?)");
        }
    }
}

} // namespace citysim::content
