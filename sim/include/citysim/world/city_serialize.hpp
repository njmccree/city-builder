#pragma once

// Save/load of the cities (architecture §10, §15.1; pattern in citysim/save/archive.hpp). Field order is the
// save format: append with a version bump, never reorder.

#include "citysim/save/archive.hpp"
#include "citysim/world/city_commands.hpp"
#include "citysim/world/city_defines.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/terrain_serialize.hpp"

#include <cstdint>
#include <vector>

namespace citysim::world {

inline constexpr std::size_t k_max_city_name_bytes = 64;

template <class Ar> void serialize(Ar& ar, CityDefines& defines, std::uint32_t version) {
    save::io(ar, defines.max_player_cities, version);
    save::io(ar, defines.min_land_fraction, version);
    save::io(ar, defines.active_ring_tiles, version);
}

template <class Ar> void serialize(Ar& ar, CityInfo& city, std::uint32_t version) {
    save::io(ar, city.id, version);
    save::io(ar, city.name, version);
    save::io(ar, city.first_tile, version);
    save::io(ar, city.founded_on.year, version);
    save::io(ar, city.founded_on.month, version);
    save::io(ar, city.founded_on.day, version);
    save::io(ar, city.mode, version);
}

template <class Ar> void serialize(Ar& ar, CityRegistry& registry, std::uint32_t version) {
    std::vector<CityInfo> cities = registry.cities();
    std::uint32_t next_id = registry.next_id();
    save::io(ar, cities, version);
    save::io(ar, next_id, version);
    if constexpr (Ar::is_loading) {
        // Refuse data that would break the registry's rules instead of loading a state that cannot happen.
        std::uint32_t previous = 0;
        for (std::size_t i = 0; i < cities.size(); ++i) {
            const CityInfo& city = cities[i];
            if (city.id == 0 || city.id <= previous || city.id >= next_id) {
                ar.fail("the saved city ids are not increasing, or reach the next id");
            }
            if (city.name.size() > k_max_city_name_bytes) {
                ar.fail("a saved city name is too long");
            }
            if (!time::is_valid(city.founded_on)) {
                ar.fail("a saved city has an invalid founding date");
            }
            if (static_cast<std::uint8_t>(city.mode) > static_cast<std::uint8_t>(SimMode::Aggregate)) {
                ar.fail("a saved city has an unknown sim mode");
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (areas_overlap(cities[j].area(), city.area())) {
                    ar.fail("two saved cities overlap");
                }
            }
            previous = city.id;
        }
        registry.restore(std::move(cities), next_id);
    }
}

template <class Ar> void serialize(Ar& ar, ActiveCity& active, std::uint32_t version) {
    save::io(ar, active.city_id, version);
}

template <class Ar> void serialize(Ar& ar, FoundCity& command, std::uint32_t version) {
    save::io(ar, command.origin_tx, version);
    save::io(ar, command.origin_ty, version);
    save::io(ar, command.name, version);
}

template <class Ar> void serialize(Ar& ar, SetActiveCity& command, std::uint32_t version) {
    save::io(ar, command.city_id, version);
}

} // namespace citysim::world
