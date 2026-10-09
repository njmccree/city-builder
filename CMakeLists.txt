cmake_minimum_required(VERSION 3.25)
project(citysim VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CITYSIM_BUILD_TESTS "Build unit tests" ON)
option(CITYSIM_BUILD_TOOLS "Build CLI tools" ON)
option(CITYSIM_BUILD_GDEXT "Build the GDExtension bridge" OFF)
option(CITYSIM_WARNINGS_AS_ERRORS "Treat warnings in our code as errors (CI)" OFF)
option(CITYSIM_TRACY "Enable Tracy profiling" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Warnings)
include(Dependencies)

if(CITYSIM_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(sim)
