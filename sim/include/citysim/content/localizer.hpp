#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace citysim::content {

// One named value for a {placeholder}.
struct LocArg {
    std::string name;
    std::string value;
};

// Looks up player-facing text by key (architecture §9). Tables are flat key -> text maps per language; text
// may hold {named} placeholders, and "{{" / "}}" stand for literal braces.
//
// Lookup falls back from the current language to "en" and then to the key itself, so a missing string is
// visible instead of blank; every such miss is counted. Content loading fills it, mod by mod in load order,
// so a later mod's text for a key replaces an earlier one. Presentation only: it is not part of the
// simulation state and not tied to a clock.
class Localizer {
  public:
    static constexpr std::string_view k_fallback_language = "en";

    // Adds or replaces one string.
    void set(const std::string& language, const std::string& key, std::string text);

    // The language get() tries first. A language with no strings is allowed (everything falls back).
    void set_language(std::string language) { language_ = std::move(language); }
    const std::string& language() const { return language_; }

    bool has(const std::string& language, const std::string& key) const;
    // Languages that have at least one string, sorted.
    std::vector<std::string> languages() const;
    // Every key of a language, sorted; empty for an unknown language.
    std::vector<std::string> keys(const std::string& language) const;

    // The text of `key` in exactly `language`, or an empty string if it has none (no fallback, no counting).
    // For exporting a table, such as the Godot Translation bridge.
    std::string text(const std::string& language, const std::string& key) const;

    // The text for `key`: current language, then "en", then the key itself.
    std::string get(const std::string& key) const;

    // get() with {name} placeholders replaced. A placeholder with no matching argument stays as written, so
    // the gap is visible too.
    std::string format(const std::string& key, const std::vector<LocArg>& args) const;

    // How many get()/format() calls found no text in either language since construction.
    std::uint64_t missing_key_count() const { return missing_.load(std::memory_order_relaxed); }

  private:
    std::string language_{k_fallback_language};
    std::map<std::string, std::map<std::string, std::string>> tables_; // language -> key -> text
    mutable std::atomic<std::uint64_t> missing_{0};
};

// Replaces {name} in `text` from `args`; see Localizer::format.
std::string substitute_placeholders(std::string_view text, const std::vector<LocArg>& args);

} // namespace citysim::content
