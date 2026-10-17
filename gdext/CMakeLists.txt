# The GDExtension bridge (architecture §3, §11): the only code that includes Godot headers. Built when
# CITYSIM_BUILD_GDEXT is ON; the output goes to game/bin/ where game/bin/citysim.gdextension finds it.

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/godot-cpp/CMakeLists.txt")
    message(FATAL_ERROR "third_party/godot-cpp is missing: run `git submodule update --init --recursive`")
endif()

# godot-cpp options, set before it is added. They are cached values godot-cpp reads while configuring.
#   * api_version matches the Godot pinned in docs/versions.md.
#   * Exceptions stay ON: the simulation core throws (and the bridge will catch) standard exceptions.
#   * Its headers count as system headers, so our warning flags apply only to our own sources.
set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Godot API version to bind (see docs/versions.md)" FORCE)
set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
# Debug builds bind the debug template (usable in the editor and in debug exports); anything else binds release.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(GODOTCPP_TARGET "template_debug" CACHE STRING "" FORCE)
    set(CITYSIM_GDEXT_CONFIG "debug")
else()
    set(GODOTCPP_TARGET "template_release" CACHE STRING "" FORCE)
    set(CITYSIM_GDEXT_CONFIG "release")
endif()

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/godot-cpp" "${CMAKE_BINARY_DIR}/godot-cpp")

add_library(citysim_gdext SHARED
    src/city_sim.cpp
    src/register_types.cpp)
target_link_libraries(citysim_gdext PRIVATE citysim_core godot-cpp)
target_compile_features(citysim_gdext PRIVATE cxx_std_20)
citysim_set_warnings(citysim_gdext)

# libcitysim_gdext.linux.<debug|release>.x86_64.so / citysim_gdext.windows.<debug|release>.x86_64.dll
if(WIN32)
    set(CITYSIM_GDEXT_PLATFORM "windows")
    set(CITYSIM_GDEXT_PREFIX "")
else()
    set(CITYSIM_GDEXT_PLATFORM "linux")
    set(CITYSIM_GDEXT_PREFIX "lib")
endif()
set(CITYSIM_GDEXT_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/game/bin")
set_target_properties(citysim_gdext PROPERTIES
    PREFIX "${CITYSIM_GDEXT_PREFIX}"
    OUTPUT_NAME "citysim_gdext.${CITYSIM_GDEXT_PLATFORM}.${CITYSIM_GDEXT_CONFIG}.x86_64"
    CXX_VISIBILITY_PRESET hidden
    # The generator expression stops multi-config generators from adding a Debug/ subfolder.
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CITYSIM_GDEXT_OUTPUT_DIR}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CITYSIM_GDEXT_OUTPUT_DIR}>"
    PDB_OUTPUT_DIRECTORY "$<1:${CITYSIM_GDEXT_OUTPUT_DIR}>")
