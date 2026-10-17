# CTest driver for citysim_validate. Inputs: VALIDATE (path to the tool), EXPECTED_EXIT, ARGS (a ;-separated list).
execute_process(COMMAND ${VALIDATE} ${ARGS}
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE code)
if(NOT code EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR "citysim_validate ${ARGS} exited with ${code}, expected ${EXPECTED_EXIT}\nstdout:\n${out}\nstderr:\n${err}")
endif()
