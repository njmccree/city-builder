#include "citysim/world/city_registry.hpp"

#include <algorithm>
#include <stdexcept>

namespace citysim::world {

bool areas_overlap(const CityArea& a, const CityArea& b) {
    return a.first_tile.tx < b.first_tile.tx + k_city_area_tiles &&
           b.first_tile.tx < a.first_tile.tx + k_city_area_tiles &&
           a.first_tile.ty < b.first_tile.ty + k_city_area_tiles &&
           b.first_tile.ty < a.first_tile.ty + k_city_area_tiles;
}

std::int32_t area_gap_tiles(std::int32_t first_a, std::int32_t first_b) {
    return std::max({first_b - (first_a + k_city_area_tiles), first_a - (first_b + k_city_area_tiles), 0});
}

const CityInfo* CityRegistry::find(std::uint32_t id) const {
    const auto it =
        std::lower_bound(cities_.begin(), cities_.end(), id,
                         [](const CityInfo& city, std::uint32_t wanted) { return city.id < wanted; });
    return (it != cities_.end() && it->id == id) ? &*it : nullptr;
}

CityInfo* CityRegistry::find(std::uint32_t id) {
    return const_cast<CityInfo*>(static_cast<const CityRegistry*>(this)->find(id)); // NOLINT
}

bool CityRegistry::overlaps(const CityArea& candidate) const {
    return std::any_of(cities_.begin(), cities_.end(),
                       [&](const CityInfo& city) { return areas_overlap(city.area(), candidate); });
}

const CityInfo& CityRegistry::add(std::string name, TileCoord first_tile, time::Date founded_on,
                                  SimMode mode) {
    cities_.push_back(CityInfo{next_id_++, std::move(name), first_tile, founded_on, mode});
    return cities_.back();
}

std::vector<std::uint32_t> CityRegistry::neighbors(std::uint32_t id) const {
    std::vector<std::uint32_t> out;
    const CityInfo* self = find(id);
    if (self == nullptr) {
        return out;
    }
    for (const CityInfo& other : cities_) {
        if (other.id != id && area_gap_tiles(self->first_tile.tx, other.first_tile.tx) <= 1 &&
            area_gap_tiles(self->first_tile.ty, other.first_tile.ty) <= 1) {
            out.push_back(other.id);
        }
    }
    return out; // cities_ is sorted by id
}

void CityRegistry::restore(std::vector<CityInfo> cities, std::uint32_t next_id) {
    cities_ = std::move(cities);
    next_id_ = next_id;
}

} // namespace citysim::world
