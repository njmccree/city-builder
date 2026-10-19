add_library(citysim_core STATIC
    src/content/content_database.cpp
    src/content/content_id.cpp
    src/content/date_text.cpp
    src/content/era_def.cpp
    src/content/json_merge_patch.cpp
    src/content/localizer.cpp
    src/content/map_def.cpp
    src/content/resource_def.cpp
    src/content/mod_discovery.cpp
    src/content/mod_load_order.cpp
    src/content/mod_manifest.cpp
    src/content/parse_context.cpp
    src/core/command_dispatcher.cpp
    src/core/command_queue.cpp
    src/core/commands.cpp
    src/core/component_registry.cpp
    src/core/event_bus.cpp
    src/core/input_log.cpp
    src/core/isystem.cpp
    src/core/job_system.cpp
    src/core/log.cpp
    src/core/rng.cpp
    src/core/scheduler.cpp
    src/core/simulation.cpp
    src/core/state_hasher.cpp
    src/core/version.cpp
    src/core/world.cpp
    src/time/calendar.cpp
    src/time/calendar_module.cpp
    src/time/calendar_scheduler.cpp
    src/time/calendar_system.cpp
    src/time/clock_system.cpp
    src/time/game_clock.cpp
    src/time/time_module.cpp
    src/save/binary_reader.cpp
    src/save/binary_writer.cpp
    src/save/csav_file.cpp
    src/save/entity_remap.cpp
    src/save/migration_registry.cpp
    src/save/simulation_io.cpp
    src/save/world_io.cpp
    src/debug/agent_snapshot.cpp
    src/debug/grid_pathfinder.cpp
    src/debug/movement_system.cpp
    src/debug/terrain_walk_grid.cpp
    src/debug/terrain_walk_system.cpp
    src/debug/test_map.cpp
    src/debug/walk_grid.cpp
    src/debug/walking_skeleton.cpp
    src/debug/wander_planner_system.cpp
    src/runner/clock.cpp
    src/runner/game_session.cpp
    src/runner/simulation_runner.cpp
    src/runner/snapshot_view.cpp
    src/world/city_module.cpp
    src/world/city_registry.cpp
    src/world/city_tile.cpp
    src/world/flood_map.cpp
    src/world/flood_module.cpp
    src/world/loaded_map.cpp
    src/world/map_package.cpp
    src/world/raster.cpp
    src/world/resource_fields.cpp
    src/world/resource_module.cpp
    src/world/river_network.cpp
    src/world/terraform.cpp
    src/world/terraform_module.cpp
    src/world/terrain_publisher.cpp
    src/world/terrain_deltas.cpp
    src/world/terrain_synth.cpp
    src/world/water_queries.cpp
    src/world/water_state.cpp
    src/world/world_module.cpp
    src/world/world_terrain.cpp)
target_include_directories(citysim_core PUBLIC include)
target_compile_features(citysim_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(citysim_core
    PUBLIC Threads::Threads EnTT::EnTT nlohmann_json::nlohmann_json spdlog::spdlog
    PRIVATE zstd::libzstd xxHash::xxhash)
citysim_set_warnings(citysim_core)

# Terrain heights must be the same bits on every compiler and CPU (golden hashes, architecture §15.4), so the
# synthesis never fuses a multiply and an add into one rounding.
if(NOT MSVC)
    set_source_files_properties(src/world/terrain_synth.cpp src/world/terraform.cpp
        src/debug/terrain_walk_grid.cpp src/world/resource_fields.cpp
        src/world/loaded_map.cpp PROPERTIES
        COMPILE_OPTIONS -ffp-contract=off)
endif()

# Profiler zones compile to nothing unless this is on (see citysim/profiling/profile.hpp).
if(CITYSIM_TRACY)
    target_link_libraries(citysim_core PUBLIC Tracy::TracyClient)
    target_compile_definitions(citysim_core PUBLIC CITYSIM_TRACY_ENABLED=1)
endif()

# Tools come first: the unit tests also link the headless support library when it exists.
if(CITYSIM_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(CITYSIM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
