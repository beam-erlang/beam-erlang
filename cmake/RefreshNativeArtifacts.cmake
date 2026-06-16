# Separately shipped modern native wrappers retain committed artifacts.
set(_entries zlib/zlib tracing/erl_tracer)
set(_include "${SOURCE_DIR}/src/beam/file")
set(_sources)
foreach(_entry IN LISTS _entries)
    list(APPEND _sources "${SOURCE_DIR}/src/beam/${_entry}.erl")
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(COMMAND "${BOOTSTRAP}" --deterministic
    -I "${SOURCE_DIR}/src/beam/support/include" -I "${_include}"
    -o "${OUTPUT_DIR}" -- ${_sources}
    RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Failed to compile BEAM native wrappers: ${_status}")
endif()
