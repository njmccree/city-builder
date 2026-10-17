#pragma once

#include <godot_cpp/core/class_db.hpp>

namespace citysim::gdext {

void initialize_citysim_module(godot::ModuleInitializationLevel level);
void uninitialize_citysim_module(godot::ModuleInitializationLevel level);

} // namespace citysim::gdext
