# Third-party dependencies, pinned to release tags (record changes in docs/versions.md).
include(FetchContent)

set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v4.0.0
    GIT_SHALLOW TRUE
    SYSTEM)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE
    SYSTEM)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
    SYSTEM)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
    SYSTEM)

FetchContent_Declare(zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.7
    GIT_SHALLOW TRUE
    SYSTEM
    SOURCE_SUBDIR build/cmake)

FetchContent_Declare(xxHash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.4
    GIT_SHALLOW TRUE
    SYSTEM
    SOURCE_SUBDIR build/cmake)

# SYSTEM marks third-party include dirs as system headers so our warning flags never apply to them.

# zstd: static library only, no programs or tests.
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# xxHash: library only.
set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# nlohmann_json / spdlog / EnTT / Catch2: skip their own tests and examples.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(ENTT_INSTALL OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(EnTT nlohmann_json spdlog zstd xxHash)
if(CITYSIM_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

# Normalise target names across the zstd/xxHash packaging variants.
if(NOT TARGET zstd::libzstd AND TARGET libzstd_static)
    add_library(zstd::libzstd ALIAS libzstd_static)
endif()
if(NOT TARGET xxHash::xxhash AND TARGET xxhash)
    add_library(xxHash::xxhash ALIAS xxhash)
endif()
