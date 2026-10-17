#include "citysim/content/localizer.hpp"

#include <algorithm>

namespace citysim::content {

void Localizer::set(const std::string& language, const std::string& key, std::string text) {
    tables_[language][key] = std::move(text);
}

bool Localizer::has(const std::string& language, const std::string& key) const {
    const auto table = tables_.find(language);
    return table != tables_.end() && table->second.contains(key);
}

std::vector<std::string> Localizer::languages() const {
    std::vector<std::string> result;
    for (const auto& entry : tables_) {
        if (!entry.second.empty()) {
            result.push_back(entry.first);
        }
    }
    return result; // std::map keeps them sorted
}

std::vector<std::string> Localizer::keys(const std::string& language) const {
    std::vector<std::string> result;
    const auto table = tables_.find(language);
    if (table != tables_.end()) {
        for (const auto& entry : table->second) {
            result.push_back(entry.first);
        }
    }
    return result;
}

std::string Localizer::get(const std::string& key) const {
    const auto find_in = [&](const std::string& language) -> const std::string* {
        const auto table = tables_.find(language);
        if (table == tables_.end()) {
            return nullptr;
        }
        const auto text = table->second.find(key);
        return text == table->second.end() ? nullptr : &text->second;
    };
    if (const std::string* text = find_in(language_)) {
        return *text;
    }
    if (const std::string* text = find_in(std::string(k_fallback_language))) {
        return *text;
    }
    missing_.fetch_add(1, std::memory_order_relaxed);
    return key;
}

std::string Localizer::format(const std::string& key, const std::vector<LocArg>& args) const {
    return substitute_placeholders(get(key), args);
}

std::string substitute_placeholders(std::string_view text, const std::vector<LocArg>& args) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) {
            result += c; // "{{" and "}}" are literal braces
            ++i;
        } else if (c == '{') {
            const std::size_t close = text.find('}', i + 1);
            if (close == std::string_view::npos) {
                result += c;
                continue;
            }
            const std::string_view name = text.substr(i + 1, close - i - 1);
            const auto arg = std::find_if(args.begin(), args.end(),
                                          [&](const LocArg& candidate) { return candidate.name == name; });
            if (arg != args.end()) {
                result += arg->value;
            } else {
                result.append(text.substr(i, close - i + 1)); // keep it, so the gap shows
            }
            i = close;
        } else {
            result += c;
        }
    }
    return result;
}

} // namespace citysim::content
