#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace citysim::content {

// A namespaced content id, "<mod>:<name>", e.g. "base:era.ancient" (architecture §8). Both halves are
// non-empty and use only lowercase a-z, digits, '_' and '.'; there is exactly one ':'.
class ContentId {
  public:
    ContentId() = default;
    // Throws std::invalid_argument if `text` is not a valid id.
    explicit ContentId(std::string_view text);

    // Returns nullopt for an invalid id; if `reason` is given it receives the explanation.
    static std::optional<ContentId> parse(std::string_view text, std::string* reason = nullptr);
    static bool is_valid(std::string_view text) { return !validation_error(text).has_value(); }

    // The explanation of what is wrong with `text`, or nullopt if it is valid.
    static std::optional<std::string> validation_error(std::string_view text);

    const std::string& str() const { return text_; }
    std::string_view mod() const { return std::string_view(text_).substr(0, colon_); }
    std::string_view name() const { return std::string_view(text_).substr(colon_ + 1); }
    bool empty() const { return text_.empty(); }

    auto operator<=>(const ContentId& other) const { return text_ <=> other.text_; }
    bool operator==(const ContentId& other) const { return text_ == other.text_; }

  private:
    std::string text_;
    std::size_t colon_ = 0;
};

} // namespace citysim::content
