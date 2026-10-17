#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/def_registry.hpp"
#include "citysim/content/parse_context.hpp"

#include <functional>
#include <map>
#include <optional>
#include <span>
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

    // The handle index of `id` in this type's registry, if it exists. Only valid after finalize().
    virtual std::optional<std::uint32_t> find_index(const ContentId& id) const = 0;
    // Runs the type's resolve step on every definition, in id order, positioned at where each came from.
    virtual void resolve_all(ParseContext& context) = 0;
    // Runs the type's validation on every definition, in id order.
    virtual void validate_all(ParseContext& context) = 0;
};

template <class T> using ParseFn = std::function<T(const nlohmann::json&, ParseContext&)>;

// Fills in the references of one definition (see resolve_def_ref).
template <class T> using ResolveFn = std::function<void(T&, ParseContext&)>;

// Checks one definition once everything is loaded and resolved. `all` is the whole registry, for checks that
// span definitions (such as unique values). Report problems on the context, which is positioned at `def`.
template <class T> using ValidateFn = std::function<void(const T&, const DefRegistry<T>&, ParseContext&)>;

// Where a definition came from, kept for messages from the later passes.
struct DefSource {
    std::string file;
    std::string mod_root;
    std::string pointer;
};

template <class T> class DefKind final : public IDefKind {
  public:
    DefKind(std::string type_name, ParseFn<T> parse, ResolveFn<T> resolve, ValidateFn<T> validate)
        : type_name_(std::move(type_name)), parse_(std::move(parse)), resolve_(std::move(resolve)),
          validate_(std::move(validate)) {}

    std::string_view type_name() const override { return type_name_; }

    void parse_and_add(const nlohmann::json& json, const ContentId& id, ParseContext& context) override {
        const std::size_t errors_before = context.error_count();
        T value = parse_(json, context);
        if (context.error_count() != errors_before) {
            return; // never store a half-parsed definition
        }
        if (!registry_.add(id, std::move(value))) {
            const DefSource& first = sources_.at(id.str());
            context.error("duplicate id '" + id.str() + "' (first defined in " + first.file + " at " +
                          first.pointer + ")");
            return;
        }
        sources_.emplace(id.str(), DefSource{context.file(), context.mod_root(), context.pointer()});
    }

    void finalize() override { registry_.finalize(); }

    std::optional<std::uint32_t> find_index(const ContentId& id) const override {
        const auto handle = registry_.find(id);
        return handle ? std::optional<std::uint32_t>(handle->index) : std::nullopt;
    }

    void resolve_all(ParseContext& context) override {
        if (!resolve_) {
            return;
        }
        std::span<T> defs = registry_.definitions_for_resolve();
        for (std::size_t i = 0; i < defs.size(); ++i) {
            position(context, registry_.ids()[i]);
            resolve_(defs[i], context);
        }
    }

    void validate_all(ParseContext& context) override {
        if (!validate_) {
            return;
        }
        for (std::size_t i = 0; i < registry_.size(); ++i) {
            position(context, registry_.ids()[i]);
            validate_(registry_.all()[i], registry_, context);
        }
    }

    DefRegistry<T>& registry() { return registry_; }
    const DefRegistry<T>& registry() const { return registry_; }

  private:
    void position(ParseContext& context, const ContentId& id) const {
        const DefSource& source = sources_.at(id.str());
        context.set_file(source.file);
        context.set_mod_root(source.mod_root);
        context.set_pointer(source.pointer);
        context.set_def_id(id);
    }

    std::string type_name_;
    ParseFn<T> parse_;
    ResolveFn<T> resolve_;
    ValidateFn<T> validate_;
    DefRegistry<T> registry_;
    std::map<std::string, DefSource> sources_; // id -> where it was defined
};

} // namespace citysim::content
