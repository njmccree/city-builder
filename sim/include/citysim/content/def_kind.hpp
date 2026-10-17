#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/def_registry.hpp"
#include "citysim/content/parse_context.hpp"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace citysim::content {

// Type-erased face of one registered definition type, so the loader can dispatch on the "type" field.
class IDefKind {
  public:
    virtual ~IDefKind() = default;
    virtual std::string_view type_name() const = 0;
    // Parses one definition object and stores it, or reports why not. The context is positioned at it.
    virtual void parse_and_add(const nlohmann::json& json, const ContentId& id, ParseContext& context) = 0;
    virtual void finalize() = 0;
};

template <class T> using ParseFn = std::function<T(const nlohmann::json&, ParseContext&)>;

template <class T> class DefKind final : public IDefKind {
  public:
    DefKind(std::string type_name, ParseFn<T> parse)
        : type_name_(std::move(type_name)), parse_(std::move(parse)) {}

    std::string_view type_name() const override { return type_name_; }

    void parse_and_add(const nlohmann::json& json, const ContentId& id, ParseContext& context) override {
        const std::size_t errors_before = context.error_count();
        T value = parse_(json, context);
        if (context.error_count() != errors_before) {
            return; // never store a half-parsed definition
        }
        const std::string where = context.file() + " at " + context.pointer();
        if (!registry_.add(id, std::move(value))) {
            context.error("duplicate id '" + id.str() + "' (first defined in " + first_seen_.at(id.str()) +
                          ")");
            return;
        }
        first_seen_.emplace(id.str(), where);
    }

    void finalize() override { registry_.finalize(); }

    DefRegistry<T>& registry() { return registry_; }
    const DefRegistry<T>& registry() const { return registry_; }

  private:
    std::string type_name_;
    ParseFn<T> parse_;
    DefRegistry<T> registry_;
    std::map<std::string, std::string> first_seen_; // id -> "file at pointer", for duplicate messages
};

} // namespace citysim::content
