add_executable(citysim_tests
    core/component_registry_tests.cpp
    core/rng_tests.cpp
    core/simulation_determinism_tests.cpp
    core/simulation_tests.cpp
    core/state_hasher_tests.cpp
    core/version_tests.cpp
    core/world_hash_tests.cpp
    core/world_tests.cpp)
target_include_directories(citysim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(citysim_tests PRIVATE citysim_core Catch2::Catch2WithMain)
citysim_set_warnings(citysim_tests)

include(Catch)
catch_discover_tests(citysim_tests)
