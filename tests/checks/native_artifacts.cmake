include("${SOURCE_DIR}/cmake/RefreshNativeArtifacts.cmake")
set(_pairs tracing:erl_tracer zlib:zlib)
foreach(_pair IN LISTS _pairs)
    string(REPLACE ":" ";" _parts "${_pair}")
    list(GET _parts 0 _component)
    list(GET _parts 1 _module)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
        "${OUTPUT_DIR}/${_module}.beam"
        "${SOURCE_DIR}/src/${RUNTIME}/${_component}/ebin/${_module}.beam"
        RESULT_VARIABLE _status)
    if(NOT _status EQUAL 0)
        message(FATAL_ERROR "${RUNTIME} ${_module} differs from its deterministic source build")
    endif()
endforeach()
