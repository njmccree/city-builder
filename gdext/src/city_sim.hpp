#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

namespace citysim::gdext {

// The node the game talks to (architecture §11), registered as the autoload `Sim` later. For now it only
// proves the bridge works: it reports the version of the simulation core it is linked with.
class CitySim : public godot::Node {
    GDCLASS(CitySim, godot::Node)

  protected:
    static void _bind_methods();

  public:
    godot::String get_core_version() const;
};

} // namespace citysim::gdext
