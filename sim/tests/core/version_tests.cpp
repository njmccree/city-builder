#include "citysim/core/version.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version is non-empty", "[core]") {
    REQUIRE_FALSE(citysim::core::version().empty());
}
