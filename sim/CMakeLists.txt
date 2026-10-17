add_library(citysim_core STATIC
    src/content/content_database.cpp
    src/content/content_id.cpp
    src/content/era_def.cpp
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
    src/time/calendar_system.cpp
    src/time/clock_system.cpp
    src/time/game_clock.cpp
    src/time/time_module.cpp
    src/runner/clock.cpp
    src/runner/simulation_runner.cpp)
target_include_directories(citysim_core PUBLIC include)
target_compile_features(citysim_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(citysim_core
    PUBLIC Threads::Threads EnTT::EnTT nlohmann_json::nlohmann_json spdlog::spdlog
    PRIVATE zstd::libzstd xxHash::xxhash)
citysim_set_warnings(citysim_core)

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
