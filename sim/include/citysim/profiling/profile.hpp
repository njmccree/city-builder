#pragma once

// Optional profiler zones (architecture §3). With -DCITYSIM_TRACY=ON these map to Tracy; otherwise every
// macro compiles to nothing, so zones cost nothing in normal builds and can never affect the simulation.
//
//     void System::update(TickContext&) {
//         CS_PROFILE_ZONE("my_system.update");            // name must be a string literal
//     }
//     CS_PROFILE_ZONE_DYNAMIC(system->name());             // a name known only at run time
//     (std::string_view)
//
// A scope can hold one zone macro; open an inner block for a second one.

#if defined(CITYSIM_TRACY_ENABLED)

#include <tracy/Tracy.hpp>

#define CS_PROFILE_ZONE(name) ZoneScopedN(name)
#define CS_PROFILE_ZONE_DYNAMIC(name_view)                                                                   \
    ZoneScoped;                                                                                              \
    ZoneName((name_view).data(), (name_view).size())
#define CS_PROFILE_FRAME_MARK FrameMark

#else

#define CS_PROFILE_ZONE(name) ((void)0)
// sizeof does not evaluate its operand, so the name costs nothing but still counts as used.
#define CS_PROFILE_ZONE_DYNAMIC(name_view) ((void)sizeof(name_view))
#define CS_PROFILE_FRAME_MARK ((void)0)

#endif
