find_package(Threads REQUIRED)
add_executable(citysim_tests
    content/content_tests.cpp
    core/commands_tests.cpp
    core/component_registry_tests.cpp
    core/determinism_harness_tests.cpp
    core/job_system_tests.cpp
    core/log_tests.cpp
    core/rng_tests.cpp
    core/simulation_determinism_tests.cpp
    core/simulation_tests.cpp
    core/state_hasher_tests.cpp
    core/version_tests.cpp
    core/world_hash_tests.cpp
    core/world_tests.cpp
    runner/runner_tests.cpp
    runner/triple_buffer_tests.cpp
    time/calendar_system_tests.cpp
    time/calendar_tests.cpp
    time/clock_tests.cpp)
target_include_directories(citysim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(citysim_tests PRIVATE CITYSIM_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    CITYSIM_GAME_CONTENT_DIR="${CMAKE_SOURCE_DIR}/game/content")
target_link_libraries(citysim_tests PRIVATE citysim_core Catch2::Catch2WithMain Threads::Threads)
citysim_set_warnings(citysim_tests)

# The headless support library (log text format, CLI parsing) has unit tests of its own.
if(TARGET citysim_headless_support)
    target_sources(citysim_tests PRIVATE tools/headless_support_tests.cpp)
    target_link_libraries(citysim_tests PRIVATE citysim_headless_support)
endif()

include(Catch)
catch_discover_tests(citysim_tests)
