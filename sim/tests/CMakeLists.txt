find_package(Threads REQUIRED)
add_executable(citysim_tests
    content/content_tests.cpp
    content/mods_tests.cpp
    debug/agent_snapshot_tests.cpp
    debug/pathfinding_tests.cpp
    debug/walking_skeleton_tests.cpp
    save/archive_tests.cpp
    save/csav_tests.cpp
    save/fixture_save_tests.cpp
    save/simulation_io_tests.cpp
    save/time_save_tests.cpp
    save/world_io_tests.cpp
    content/layering_tests.cpp
    content/localization_tests.cpp
    content/map_def_tests.cpp
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
    runner/game_session_tests.cpp
    runner/runner_save_tests.cpp
    runner/snapshot_view_tests.cpp
    runner/runner_tests.cpp
    runner/triple_buffer_tests.cpp
    time/calendar_events_tests.cpp
    time/calendar_system_tests.cpp
    time/calendar_tests.cpp
    time/clock_tests.cpp
    world/frames_tests.cpp
    world/package_tests.cpp
    world/raster_tests.cpp
    world/city_tests.cpp
    world/flood_tests.cpp
    world/terraform_tests.cpp
    debug/terrain_walk_tests.cpp
    world/terrain_deltas_tests.cpp
    world/terrain_synth_tests.cpp
    world/water_tests.cpp)
target_include_directories(citysim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(citysim_tests PRIVATE CITYSIM_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    CITYSIM_GAME_CONTENT_DIR="${CMAKE_SOURCE_DIR}/game/content"
    CITYSIM_SYNTHETIC_MAP_DIR="${CMAKE_BINARY_DIR}/test_data/synthetic_map")
target_link_libraries(citysim_tests PRIVATE citysim_core Catch2::Catch2WithMain Threads::Threads)
citysim_set_warnings(citysim_tests)

# The headless support library (log text format, CLI parsing) has unit tests of its own.
if(TARGET citysim_headless_support)
    target_sources(citysim_tests PRIVATE tools/headless_support_tests.cpp)
    target_link_libraries(citysim_tests PRIVATE citysim_headless_support)
endif()

# The synthetic map generator (citysim_mapgen --synthetic) has unit tests too.
if(TARGET citysim_mapgen_support)
    # So do the terrain tests, which build test maps with it.
    target_sources(citysim_tests PRIVATE tools/mapgen_tests.cpp world/loaded_map_tests.cpp
        world/world_module_tests.cpp world/world_terrain_tests.cpp)
    target_link_libraries(citysim_tests PRIVATE citysim_mapgen_support)
endif()

# Tests tagged [synthetic_map] read the full-size synthetic package, which a CTest fixture generates first
# (sim/tools/mapgen/CMakeLists.txt). Everything else runs without it.
include(Catch)
catch_discover_tests(citysim_tests TEST_SPEC "~[synthetic_map]")
catch_discover_tests(citysim_tests TEST_SPEC "[synthetic_map]" TEST_PREFIX "synthetic_map: "
    PROPERTIES FIXTURES_REQUIRED synthetic_map)
