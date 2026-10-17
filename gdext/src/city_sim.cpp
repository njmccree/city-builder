#include "city_sim.hpp"

#include "citysim/core/version.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace citysim::gdext {

void CitySim::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("get_core_version"), &CitySim::get_core_version);
}

godot::String CitySim::get_core_version() const {
    const std::string_view version = core::version();
    return godot::String::utf8(version.data(), static_cast<int>(version.size()));
}

} // namespace citysim::gdext
