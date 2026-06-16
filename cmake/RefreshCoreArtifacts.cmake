# Produce reviewable candidates without changing shared committed artifacts.
set(CMAKE_SOURCE_DIR "${SOURCE_DIR}")
include("${SOURCE_DIR}/cmake/RuntimeCoreModules.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(
    COMMAND "${BOOTSTRAP}" --deterministic --debug-info
        -I "${SOURCE_DIR}/src/beam/support/include"
        -o "${OUTPUT_DIR}" -- ${BEAM_CORE_SOURCES}
    RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Failed to compile shared core artifacts: ${_status}")
endif()
if(VERIFY)
    foreach(_seed IN LISTS BEAM_CORE_BEAM_FILES)
        get_filename_component(_name "${_seed}" NAME)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${OUTPUT_DIR}/${_name}" "${_seed}" RESULT_VARIABLE _status)
        if(NOT _status EQUAL 0)
            message(FATAL_ERROR "${_name} differs from its embedded seed; refresh and review the core artifact")
        endif()
    endforeach()
endif()
