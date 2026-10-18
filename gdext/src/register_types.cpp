#include "register_types.hpp"

#include "agent_renderer.hpp"
#include "city_sim.hpp"
#include "test_map_view.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>

namespace citysim::gdext {

void initialize_citysim_module(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(CitySim);
    GDREGISTER_CLASS(AgentRenderer);
    GDREGISTER_CLASS(TestMapView);
}

void uninitialize_citysim_module(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

} // namespace citysim::gdext

// The entry point Godot calls; its name is `entry_symbol` in game/bin/citysim.gdextension.
extern "C" {
GDExtensionBool GDE_EXPORT citysim_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                const GDExtensionClassLibraryPtr library,
                                                GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(citysim::gdext::initialize_citysim_module);
    init.register_terminator(citysim::gdext::uninitialize_citysim_module);
    init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
}
