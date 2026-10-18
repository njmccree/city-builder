#include "citysim/content/content_id.hpp"

#include <stdexcept>

namespace citysim::content {

namespace {
bool is_id_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}
} // namespace

std::optional<std::string> ContentId::validation_error(std::string_view text) {
    if (text.empty()) {
        return "id is empty";
    }
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        return "id must look like '<mod>:<name>' (no ':' found)";
    }
    if (text.find(':', colon + 1) != std::string_view::npos) {
        return "id must contain exactly one ':'";
    }
    if (colon == 0) {
        return "the mod part before ':' is empty";
    }
    if (colon + 1 == text.size()) {
        return "the name part after ':' is empty";
    }
    for (const char c : text) {
        if (c != ':' && !is_id_char(c)) {
            return std::string("invalid character '") + c + "' (allowed: a-z, 0-9, '_', '.' and one ':')";
        }
    }
    return std::nullopt;
}

std::optional<ContentId> ContentId::parse(std::string_view text, std::string* reason) {
    if (const auto problem = validation_error(text)) {
        if (reason != nullptr) {
            *reason = *problem;
        }
        return std::nullopt;
    }
    ContentId id;
    id.text_ = std::string(text);
    id.colon_ = text.find(':');
    return id;
}

ContentId::ContentId(std::string_view text) {
    std::string reason;
    const auto parsed = parse(text, &reason);
    if (!parsed) {
        throw std::invalid_argument("invalid content id '" + std::string(text) + "': " + reason);
    }
    *this = *parsed;
}

} // namespace citysim::content
