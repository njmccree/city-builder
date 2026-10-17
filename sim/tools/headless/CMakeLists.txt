# Everything except main() lives in a library so the unit tests can use it too.
add_library(citysim_headless_support STATIC
    bench.cpp
    cli_options.cpp
    demo_scenario.cpp
    input_log_text.cpp)
target_include_directories(citysim_headless_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(citysim_headless_support PUBLIC citysim_core)
citysim_set_warnings(citysim_headless_support)

add_executable(citysim_headless main.cpp)
target_link_libraries(citysim_headless PRIVATE citysim_headless_support)
citysim_set_warnings(citysim_headless)

if(CITYSIM_BUILD_TESTS)
    add_test(NAME headless_is_deterministic
        COMMAND ${CMAKE_COMMAND} -DHEADLESS=$<TARGET_FILE:citysim_headless> -DMODE=determinism
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/test_headless.cmake)
    add_test(NAME headless_replay_matches_recording
        COMMAND ${CMAKE_COMMAND} -DHEADLESS=$<TARGET_FILE:citysim_headless> -DMODE=replay
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/test_headless.cmake)
    add_test(NAME headless_resumes_from_a_save
        COMMAND ${CMAKE_COMMAND} -DHEADLESS=$<TARGET_FILE:citysim_headless> -DMODE=save_load
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/test_headless.cmake)
    add_test(NAME headless_bench_runs
        COMMAND ${CMAKE_COMMAND} -DHEADLESS=$<TARGET_FILE:citysim_headless> -DMODE=bench
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/test_headless.cmake)
endif()
