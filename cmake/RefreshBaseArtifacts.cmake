# Refresh candidates are reviewable and never replace tracked seeds automatically.
foreach(_required IN ITEMS MODULES SOURCES)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required from the base producer")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(COMMAND "${BOOTSTRAP}" --deterministic -o "${OUTPUT_DIR}" -- ${SOURCES}
    RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Failed to compile embedded BEAM base candidates: ${_status}")
endif()
if(VERIFY)
    foreach(_module IN LISTS MODULES)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${OUTPUT_DIR}/${_module}.beam" "${SOURCE_DIR}/src/beam/library/ebin/${_module}.beam"
            RESULT_VARIABLE _status)
        if(NOT _status EQUAL 0)
            message(FATAL_ERROR "${_module} differs from its embedded seed; refresh and review the base artifact")
        endif()
    endforeach()
endif()
