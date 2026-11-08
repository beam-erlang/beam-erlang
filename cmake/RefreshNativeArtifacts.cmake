# Separately shipped modern native wrappers retain committed artifacts.
set(_entries zlib/zlib tracing/beam_trace tracing/beam_runtime_trace_errors)
set(_include "${SOURCE_DIR}/src/beam/file")
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(_projection "${SOURCE_DIR}/src/beam/runtime_support")
file(MAKE_DIRECTORY "${OUTPUT_DIR}/src")
execute_process(COMMAND "${BOOTSTRAP}" --deterministic -o "${OUTPUT_DIR}/src" --
    "${_projection}/beam_runtime_projection.erl" RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Failed to compile the owned runtime projection: ${_status}")
endif()
set(_sources)
foreach(_entry IN LISTS _entries)
    get_filename_component(_module "${_entry}" NAME)
    set(_staged "${OUTPUT_DIR}/src/${_module}.erl")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${_projection}/stage.py"
        --source "${SOURCE_DIR}/src/beam/${_entry}.erl" --output "${_staged}"
        --transform "${_projection}/beam_runtime_projection.erl"
        RESULT_VARIABLE _status)
    if(NOT _status EQUAL 0)
        message(FATAL_ERROR "Failed to stage ${_module}: ${_status}")
    endif()
    list(APPEND _sources "${_staged}")
endforeach()
execute_process(COMMAND "${BOOTSTRAP}" --deterministic --modern-target
    -I "${SOURCE_DIR}/src/beam/support/include" -I "${_include}"
    -o "${OUTPUT_DIR}" -- ${_sources}
    RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Failed to compile BEAM native wrappers: ${_status}")
endif()
