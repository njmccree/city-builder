#pragma once

namespace citysim::world {

// Numbers that shape the synthesized terrain (architecture §15.4). The built-in defaults are these; content
// loading overrides them from defines/terrain.json. They decide what the generator makes, so they belong to
// the terrain's identity like the map package and the generator version: the content hash covers
// defines/terrain.json, and a save made with other content warns on load.
struct TerrainDefines {
    // How much of a channel's depth is left at its edges: the bed is deepest in the middle and rises as a
    // parabola to this fraction of the depth at the edge (0 = a bed as deep as the middle everywhere, 1 =
    // flat at the water level). 0..1.
    float channel_edge_depth_fraction = 0.3F;
    // Beyond the channel's edge the bank climbs from the bed back to the surrounding ground over this many
    // meters (a smooth S-curve). 1..200.
    float bank_width_m = 24.0F;
};

} // namespace citysim::world
