# Everything except main() lives in a library so the unit tests can use it too.
add_library(citysim_mapgen_support STATIC synthetic_map.cpp)
target_include_directories(citysim_mapgen_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(citysim_mapgen_support PUBLIC citysim_core)
citysim_set_warnings(citysim_mapgen_support)

add_executable(citysim_mapgen main.cpp)
target_link_libraries(citysim_mapgen PRIVATE citysim_mapgen_support)
citysim_set_warnings(citysim_mapgen)

if(CITYSIM_BUILD_TESTS)
    # The synthetic package the unit tests that need a full-size map read (sim/tests/support/synthetic_map.hpp).
    # Generated once per ctest run; those tests are registered with FIXTURES_REQUIRED synthetic_map.
    add_test(NAME synthetic_map_is_generated
        COMMAND citysim_mapgen --synthetic --out ${CMAKE_BINARY_DIR}/test_data/synthetic_map --seed 1)
    set_tests_properties(synthetic_map_is_generated PROPERTIES FIXTURES_SETUP synthetic_map)
endif()
