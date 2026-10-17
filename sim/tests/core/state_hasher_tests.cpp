#include "citysim/core/state_hasher.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

using citysim::core::StateHasher;

TEST_CASE("StateHasher golden value", "[core][hash]") {
    // Changing this changes every state hash and golden scenario.
    StateHasher hasher;
    hasher.add(std::string_view("citysim"));
    hasher.add_pod(std::uint32_t{42});
    REQUIRE(hasher.digest() == 0x5f45314baba4cafeULL); // XXH3_64bits of the same bytes, checked independently
}

TEST_CASE("StateHasher is deterministic and order sensitive", "[core][hash]") {
    StateHasher a;
    StateHasher b;
    a.add_pod(std::uint32_t{1});
    a.add_pod(std::uint32_t{2});
    b.add_pod(std::uint32_t{1});
    b.add_pod(std::uint32_t{2});
    REQUIRE(a.digest() == b.digest());
    REQUIRE(a.digest() == a.digest()); // digest() does not consume state

    StateHasher swapped;
    swapped.add_pod(std::uint32_t{2});
    swapped.add_pod(std::uint32_t{1});
    REQUIRE(swapped.digest() != a.digest());
}

TEST_CASE("StateHasher strings are length-prefixed", "[core][hash]") {
    StateHasher a;
    a.add(std::string_view("ab"));
    a.add(std::string_view("c"));
    StateHasher b;
    b.add(std::string_view("a"));
    b.add(std::string_view("bc"));
    REQUIRE(a.digest() != b.digest());
}

TEST_CASE("StateHasher normalizes -0 and NaN", "[core][hash]") {
    const auto hash_of = [](auto value) {
        StateHasher hasher;
        hasher.add_float(value);
        return hasher.digest();
    };
    REQUIRE(hash_of(0.0F) == hash_of(-0.0F));
    REQUIRE(hash_of(0.0) == hash_of(-0.0));
    REQUIRE(hash_of(std::numeric_limits<float>::quiet_NaN()) ==
            hash_of(-std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(hash_of(std::numeric_limits<double>::quiet_NaN()) ==
            hash_of(std::numeric_limits<double>::signaling_NaN()));
    REQUIRE(hash_of(1.0F) != hash_of(2.0F));
    REQUIRE(hash_of(1.0) != hash_of(1.0000000000000002));
}

TEST_CASE("StateHasher is movable", "[core][hash]") {
    StateHasher a;
    a.add_pod(std::uint8_t{7});
    const auto before = a.digest();
    StateHasher b = std::move(a);
    REQUIRE(b.digest() == before);
}
