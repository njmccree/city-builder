#pragma once

#include <cstdint>
#include <vector>

namespace citysim::debug {

// Components of the walking skeleton's debug agents. All state is plain data; all are registered (hash and
// save) by debug::install. Positions are ground coordinates in meters, (x, z), world origin at the south-west
// corner (architecture §11).
struct Position {
    float x = 0.0F;
    float z = 0.0F;
};

// Direction of travel in radians: yaw = atan2(dx, dz), so 0 faces +z and pi/2 faces +x.
struct Heading {
    float yaw = 0.0F;
};

// The cells still to walk (indices into the TestMap), in order; `next` is the one being walked to. Finished
// when next == cells.size(). `speed_mps` is meters per simulated second.
struct PathFollower {
    std::vector<std::uint32_t> cells;
    std::uint32_t next = 0;
    float speed_mps = 1.4F;

    bool finished() const { return next >= cells.size(); }
};

// Marks an agent that walks to random places forever. `trips` counts the destinations drawn so far, and is
// part of the random key, so every draw is new.
struct DebugWanderer {
    std::uint32_t trips = 0;
};

// Singleton: how many agents have ever been spawned, so each new agent gets fresh random draws even after a
// clear.
struct DebugAgentCounter {
    std::uint64_t spawned = 0;
};

} // namespace citysim::debug
