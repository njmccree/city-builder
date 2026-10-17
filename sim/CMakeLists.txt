add_library(citysim_core STATIC
    src/core/command_dispatcher.cpp
    src/core/command_queue.cpp
    src/core/commands.cpp
    src/core/component_registry.cpp
    src/core/event_bus.cpp
    src/core/input_log.cpp
    src/core/isystem.cpp
    src/core/rng.cpp
    src/core/scheduler.cpp
    src/core/simulation.cpp
    src/core/state_hasher.cpp
    src/core/version.cpp
    src/core/world.cpp)
target_include_directories(citysim_core PUBLIC include)
target_compile_features(citysim_core PUBLIC cxx_std_20)
target_link_libraries(citysim_core
    PUBLIC EnTT::EnTT nlohmann_json::nlohmann_json
    PRIVATE spdlog::spdlog zstd::libzstd xxHash::xxhash)
citysim_set_warnings(citysim_core)

# Tools come first: the unit tests also link the headless support library when it exists.
if(CITYSIM_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(CITYSIM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
