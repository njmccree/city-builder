#pragma once

#include "citysim/time/calendar.hpp"
#include "citysim/world/frames.hpp"
#include "citysim/world/tile_coord.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace citysim::world {

// How a city is simulated (architecture §12): individuals (Agent) or cohorts (Aggregate). Only the active
// city is Agent for now; the aggregate model itself arrives with the Citizens epic.
enum class SimMode : std::uint8_t { Agent = 0, Aggregate = 1 };

// One founded city (architecture §15.1): an 8 x 8 km area of 4 x 4 whole tiles on the shared terrain.
struct CityInfo {
    std::uint32_t id = 0; // stable, assigned in founding order starting at 1
    std::string name;
    TileCoord first_tile; // the south-west tile of the area
    time::Date founded_on;
    SimMode mode = SimMode::Aggregate;

    CityArea area() const { return CityArea{first_tile}; }

    friend bool operator==(const CityInfo&, const CityInfo&) = default;
};

// Every founded city, in founding order (so sorted by id). A World ctx() singleton, registered as
// "world.Cities". Saved and hashed. Changes only through the FoundCity and SetActiveCity commands, on the
// tick clock.
class CityRegistry {
  public:
    const std::vector<CityInfo>& cities() const { return cities_; }
    std::size_t size() const { return cities_.size(); }
    std::uint32_t next_id() const { return next_id_; }

    // The city with this id, or nullptr.
    const CityInfo* find(std::uint32_t id) const;
    CityInfo* find(std::uint32_t id);

    // True if a city of area `candidate` would overlap an existing one (touching is allowed).
    bool overlaps(const CityArea& candidate) const;

    // Adds a city with the next id and returns it.
    const CityInfo& add(std::string name, TileCoord first_tile, time::Date founded_on, SimMode mode);

    // The cities whose area touches or lies within one tile of this city's (a gap of at most one tile on both
    // axes, diagonals included), sorted by id, not including the city itself. Empty for an unknown id.
    std::vector<std::uint32_t> neighbors(std::uint32_t id) const;

    // For loading: replaces everything. The caller has checked the data (see serialize in
    // city_serialize.hpp).
    void restore(std::vector<CityInfo> cities, std::uint32_t next_id);

    friend bool operator==(const CityRegistry&, const CityRegistry&) = default;

  private:
    std::vector<CityInfo> cities_;
    std::uint32_t next_id_ = 1;
};

// Which city the player is viewing, 0 for none. A World ctx() singleton, registered as "world.ActiveCity".
// Its tiles (plus the ring) are the terrain tiles WorldTerrain keeps active.
struct ActiveCity {
    std::uint32_t city_id = 0;

    friend bool operator==(const ActiveCity&, const ActiveCity&) = default;
};

// True if the two areas share any ground (touching edges do not count).
bool areas_overlap(const CityArea& a, const CityArea& b);
// The gap in whole tiles between two areas along one axis, 0 when they touch or overlap.
std::int32_t area_gap_tiles(std::int32_t first_a, std::int32_t first_b);

} // namespace citysim::world
