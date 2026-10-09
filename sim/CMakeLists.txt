add_library(citysim_core STATIC src/core/version.cpp)
target_include_directories(citysim_core PUBLIC include)
target_compile_features(citysim_core PUBLIC cxx_std_20)
target_link_libraries(citysim_core
    PUBLIC EnTT::EnTT nlohmann_json::nlohmann_json
    PRIVATE spdlog::spdlog zstd::libzstd xxHash::xxhash)
citysim_set_warnings(citysim_core)

if(CITYSIM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
