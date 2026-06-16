# Refresh candidates are reviewable and never replace tracked seeds automatically.
set(_modules beam beam_list beam_map beam_bytes beam_text beam_format beam_time)
set(_sources)
foreach(_module IN LISTS _modules)
    list(APPEND _sources "${SOURCE_DIR}/src/beam/library/${_module}.erl")
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(COMMAND "${BOOTSTRAP}" --deterministic -o "${OUTPUT_DIR}" -- ${_sources}
    RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Failed to compile embedded BEAM base candidates: ${_status}")
endif()
if(VERIFY)
    foreach(_module IN LISTS _modules)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${OUTPUT_DIR}/${_module}.beam" "${SOURCE_DIR}/src/beam/library/ebin/${_module}.beam"
            RESULT_VARIABLE _status)
        if(NOT _status EQUAL 0)
            message(FATAL_ERROR "${_module} differs from its embedded seed; refresh and review the base artifact")
        endif()
    endforeach()
endif()
