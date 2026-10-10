add_executable(citysim_tests core/version_tests.cpp)
target_link_libraries(citysim_tests PRIVATE citysim_core Catch2::Catch2WithMain)
citysim_set_warnings(citysim_tests)

include(Catch)
catch_discover_tests(citysim_tests)
