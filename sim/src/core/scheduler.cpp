#include "citysim/core/scheduler.hpp"

#include <stdexcept>
#include <string>

namespace citysim::core {

ISystem& Scheduler::add(Phase phase, std::unique_ptr<ISystem> system) {
    if (!system) {
        throw std::logic_error("cannot register a null system");
    }
    const std::string_view name = system->name();
    for (const auto& list : phases_) {
        for (const auto& existing : list) {
            if (existing->name() == name) {
                throw std::logic_error("duplicate system name: " + std::string(name));
            }
        }
    }

    // After the checks, so a rejected system never has side effects; before the insert, so a throwing
    // on_register leaves the scheduler unchanged.
    system->on_register(world_);

    auto& list = phases_[static_cast<std::size_t>(phase)];
    list.push_back(std::move(system));
    return *list.back();
}

void Scheduler::run_phase(Phase phase, TickContext& context, SystemObserver* observer) {
    for (const auto& system : phases_[static_cast<std::size_t>(phase)]) {
        if (observer != nullptr) {
            observer->before_system(phase, system->name());
        }
        system->update(context);
        if (observer != nullptr) {
            observer->after_system(phase, system->name());
        }
    }
}

std::span<const std::unique_ptr<ISystem>> Scheduler::systems(Phase phase) const {
    return phases_[static_cast<std::size_t>(phase)];
}

std::size_t Scheduler::system_count() const {
    std::size_t total = 0;
    for (const auto& list : phases_) {
        total += list.size();
    }
    return total;
}

} // namespace citysim::core
