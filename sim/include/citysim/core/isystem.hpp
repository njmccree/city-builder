#pragma once

#include <string_view>

namespace citysim::core {

class World;
struct TickContext;

// A simulation system (architecture §5.2). Each system is registered in exactly one phase.
class ISystem {
  public:
    ISystem() = default;
    ISystem(const ISystem&) = delete;
    ISystem& operator=(const ISystem&) = delete;
    virtual ~ISystem();

    // Unique across the whole scheduler and stable for the system's lifetime.
    virtual std::string_view name() const = 0;

    virtual void update(TickContext& context) = 0;

    // Called once when the system is added, e.g. to register components or emplace singletons.
    virtual void on_register(World& world);
};

} // namespace citysim::core
