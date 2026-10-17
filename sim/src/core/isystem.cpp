#include "citysim/core/isystem.hpp"

namespace citysim::core {

// Defined here so the vtable has one home.
ISystem::~ISystem() = default;

void ISystem::on_register(World&) {}

} // namespace citysim::core
