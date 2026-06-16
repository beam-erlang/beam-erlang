# Ordinary components use the verified BEAM compiler; compiler inputs use seeds.
# Each component declares its own source files, includes and output directory.
function(beam_add_erlang_component TARGET)
    cmake_parse_arguments(PARSE_ARGV 1 ERL "" "OUTPUT_DIRECTORY;COMPILER" "SOURCES;INCLUDE_DIRECTORIES;DEPENDS")
    if(ERL_UNPARSED_ARGUMENTS OR NOT ERL_SOURCES OR NOT ERL_OUTPUT_DIRECTORY)
        message(FATAL_ERROR "${TARGET}: provide SOURCES and OUTPUT_DIRECTORY")
    endif()
    if(NOT ERL_COMPILER)
        set(ERL_COMPILER beam_bootstrap)
    endif()
    set(_outputs)
    set(_includes)
    set(_headers)
    foreach(_source IN LISTS ERL_SOURCES)
        get_filename_component(_module "${_source}" NAME_WE)
        list(APPEND _outputs "${ERL_OUTPUT_DIRECTORY}/${_module}.beam")
    endforeach()
    foreach(_include IN LISTS ERL_INCLUDE_DIRECTORIES)
        list(APPEND _includes -I "${_include}")
        file(GLOB _component_headers CONFIGURE_DEPENDS "${_include}/*.hrl")
        list(APPEND _headers ${_component_headers})
    endforeach()
    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ERL_OUTPUT_DIRECTORY}"
        COMMAND $<TARGET_FILE:${ERL_COMPILER}> --deterministic ${_includes}
            -o "${ERL_OUTPUT_DIRECTORY}" -- ${ERL_SOURCES}
        DEPENDS ${ERL_COMPILER}
            ${ERL_SOURCES} ${_headers} ${ERL_DEPENDS}
        COMMENT "Compiling ${TARGET} with the BEAM build compiler"
        VERBATIM)
    add_custom_target(${TARGET} DEPENDS ${_outputs})
endfunction()
