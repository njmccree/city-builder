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
# Which godot-cpp flavor to bind (and so which .gdextension library entry the file is for):
#   editor           the Godot editor and anything it launches (needed for the editor to load the library)
#   template_debug   debug export templates
#   template_release release export templates
# Default: Debug builds bind `editor`, others `template_release`.
set(CITYSIM_GODOT_TARGET "" CACHE STRING "godot-cpp target: editor, template_debug or template_release (default by build type)")
if(NOT CITYSIM_GODOT_TARGET)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CITYSIM_GODOT_TARGET "editor")
    else()
        set(CITYSIM_GODOT_TARGET "template_release")
    endif()
endif()
set(GODOTCPP_TARGET "${CITYSIM_GODOT_TARGET}" CACHE STRING "" FORCE)
# The part of the library file name Godot's feature tags select by.
if(CITYSIM_GODOT_TARGET STREQUAL "editor")
    set(CITYSIM_GDEXT_CONFIG "editor")
elseif(CITYSIM_GODOT_TARGET STREQUAL "template_debug")
    set(CITYSIM_GDEXT_CONFIG "debug")
else()
    set(CITYSIM_GDEXT_CONFIG "release")
endif()

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/godot-cpp" "${CMAKE_BINARY_DIR}/godot-cpp")

add_library(citysim_gdext SHARED
    src/agent_renderer.cpp
    src/city_sim.cpp
    src/command_marshal.cpp
    src/register_types.cpp
    src/test_map_view.cpp)
target_link_libraries(citysim_gdext PRIVATE citysim_core godot-cpp)
target_compile_features(citysim_gdext PRIVATE cxx_std_20)
citysim_set_warnings(citysim_gdext)

# Class documentation shown in the editor's help. The editor can crash in headless import when a registered
# class has none (seen with Godot 4.7.2), and only the editor and debug flavors carry documentation.
if(GODOTCPP_TARGET MATCHES "editor|template_debug")
    # target_doc_sources() runs Python, and godot-cpp finds it only inside its own directory scope.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    file(GLOB_RECURSE CITYSIM_GDEXT_DOC_XML LIST_DIRECTORIES NO CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/doc_classes/*.xml")
    target_doc_sources(citysim_gdext "${CITYSIM_GDEXT_DOC_XML}")
endif()

# libcitysim_gdext.linux.<editor|debug|release>.x86_64.so / citysim_gdext.windows.<editor|debug|release>.x86_64.dll
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
