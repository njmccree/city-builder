#pragma once

#include "citysim/core/isystem.hpp"
#include "citysim/core/tick_context.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace citysim::test {

// A system whose update is a lambda, for tests.
class FnSystem : public citysim::core::ISystem {
  public:
    using Fn = std::function<void(citysim::core::TickContext&)>;

    FnSystem(std::string name, Fn fn) : name_(std::move(name)), fn_(std::move(fn)) {}

    std::string_view name() const override { return name_; }
    void update(citysim::core::TickContext& context) override { fn_(context); }
    void on_register(citysim::core::World& world) override {
        if (on_register_fn) {
            on_register_fn(world);
        }
    }

    std::function<void(citysim::core::World&)> on_register_fn;

  private:
    std::string name_;
    Fn fn_;
};

inline std::unique_ptr<FnSystem> make_system(std::string name, FnSystem::Fn fn) {
    return std::make_unique<FnSystem>(std::move(name), std::move(fn));
}

} // namespace citysim::test
