cmake_minimum_required(VERSION 3.28)
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

if(CITYSIM_BUILD_GDEXT)
    # The extension is a shared library, so everything linked into it (our core and its static
    # dependencies) must be position independent. Set before the dependencies are defined.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        # godot-cpp defaults to the static release CRT (/MT), which cannot link with our core's default
        # /MDd in Debug (LNK2038). Use the DLL CRT everywhere and tell godot-cpp not to override it.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
        set(GODOTCPP_USE_STATIC_CPP OFF CACHE BOOL "" FORCE)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Warnings)
include(Dependencies)

if(CITYSIM_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(sim)

if(CITYSIM_BUILD_GDEXT)
    add_subdirectory(gdext)
endif()
