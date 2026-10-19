#pragma once

#include <godot_cpp/variant/vector3.hpp>

namespace citysim::gdext {

// How simulation positions appear in Godot's 3D space (architecture §11). The simulation's ground plane is
// x = east, z = north. Godot is right-handed with y up, so seen from above with x to the right its +z points
// down the screen: putting north on +z would draw the world as its mirror image. North is therefore Godot's
// -z ("forward"): view (x, y, z) = (east, height, -north). Every renderer and every conversion back (picking)
// goes through these functions.
inline godot::Vector3 to_view(float east, float height, float north) {
    return godot::Vector3(east, height, -north);
}
inline float view_z(float north) {
    return -north;
}
inline float north_of_view(float view_z_value) {
    return -view_z_value;
}
// A simulation heading (a compass bearing: radians from north towards east) as a rotation about Godot's y
// axis that turns a model's forward (-z) to face it.
inline float view_yaw(float bearing) {
    return -bearing;
}

} // namespace citysim::gdext
