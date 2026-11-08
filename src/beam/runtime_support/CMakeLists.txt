# Canonical support remains a compiler input; runtime support has one separate producer.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(BEAM_RUNTIME_SUPPORT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(BEAM_RUNTIME_SUPPORT_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime-support/ebin")
set(_runtime_stage "${CMAKE_BINARY_DIR}/runtime-support/src")
set(_runtime_transform "${_runtime_stage}/beam_runtime_projection.beam")
add_custom_command(OUTPUT "${_runtime_transform}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_runtime_stage}"
    COMMAND $<TARGET_FILE:beam_bootstrap> --deterministic -o "${_runtime_stage}" --
        "${CMAKE_CURRENT_SOURCE_DIR}/beam_runtime_projection.erl"
    DEPENDS beam_bootstrap "${CMAKE_CURRENT_SOURCE_DIR}/beam_runtime_projection.erl" VERBATIM)
add_custom_target(beam_runtime_projection_transform DEPENDS "${_runtime_transform}")

function(beam_add_runtime_component TARGET)
    cmake_parse_arguments(PARSE_ARGV 1 RUN "" "OUTPUT_DIRECTORY;STAGE_DIRECTORY" "SOURCES;INCLUDE_DIRECTORIES;DEPENDS;RESOURCES")
    if(RUN_UNPARSED_ARGUMENTS OR NOT RUN_SOURCES OR NOT RUN_OUTPUT_DIRECTORY)
        message(FATAL_ERROR "${TARGET}: provide runtime SOURCES and OUTPUT_DIRECTORY")
    endif()
    set(_support_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(_staged)
    set(_outputs)
    set(_includes)
    set(_headers)
    set(_source_stage "${CMAKE_BINARY_DIR}/runtime-support/src")
    set(_projection "${_source_stage}/beam_runtime_projection.beam")
    if(RUN_STAGE_DIRECTORY AND NOT RUN_STAGE_DIRECTORY STREQUAL _source_stage)
        set(_source_stage "${RUN_STAGE_DIRECTORY}")
        set(_projection "${_source_stage}/beam_runtime_projection.beam")
        add_custom_command(OUTPUT "${_projection}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_source_stage}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/runtime-support/src/beam_runtime_projection.beam" "${_projection}"
            DEPENDS beam_runtime_projection_transform
                "${CMAKE_BINARY_DIR}/runtime-support/src/beam_runtime_projection.beam" VERBATIM)
    endif()
    foreach(_source IN LISTS RUN_SOURCES)
        get_filename_component(_module "${_source}" NAME_WE)
        if(_module STREQUAL ets)
            set(_module beam_ets)
        elseif(_module STREQUAL unicode_util)
            set(_module beam_runtime_unicode_util)
        elseif(_module MATCHES "^(binary|lists|maps|string|unicode|records)$")
            set(_module "beam_${_module}")
        elseif(_module STREQUAL math)
            set(_module beam_math)
        elseif(_module STREQUAL erl_ddll)
            set(_module beam_ddll)
        elseif(_module STREQUAL erl_debugger)
            set(_module beam_debugger)
        elseif(_module MATCHES "^(queue|sets|proplists|orddict)$")
            set(_module "beam_runtime_${_module}")
        endif()
        set(_output "${_source_stage}/${_module}.erl")
        add_custom_command(OUTPUT "${_output}"
            BYPRODUCTS "${_source_stage}/${_module}.source.json"
            COMMAND "${Python3_EXECUTABLE}" "${_support_source}/stage.py"
                --source "${_source}" --output "${_output}"
                --transform "${_support_source}/beam_runtime_projection.erl"
            DEPENDS "${_source}" "${_support_source}/stage.py"
                "${_support_source}/beam_runtime_projection.erl" VERBATIM)
        list(APPEND _staged "${_output}")
        list(APPEND _outputs "${RUN_OUTPUT_DIRECTORY}/${_module}.beam")
        get_filename_component(_original_directory "${_source}" DIRECTORY)
        list(APPEND RUN_INCLUDE_DIRECTORIES "${_original_directory}")
    endforeach()
    list(REMOVE_DUPLICATES RUN_INCLUDE_DIRECTORIES)
    foreach(_include IN LISTS RUN_INCLUDE_DIRECTORIES)
        list(APPEND _includes -I "${_include}")
        file(GLOB _component_headers CONFIGURE_DEPENDS "${_include}/*.hrl")
        list(APPEND _headers ${_component_headers})
    endforeach()
    add_custom_command(OUTPUT ${_outputs}
        BYPRODUCTS "${CMAKE_BINARY_DIR}/runtime-support/attestations/${TARGET}.json"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RUN_OUTPUT_DIRECTORY}"
            "${CMAKE_BINARY_DIR}/runtime-support/attestations"
        COMMAND $<TARGET_FILE:beam_bootstrap> --deterministic --modern-target ${_includes}
            -o "${RUN_OUTPUT_DIRECTORY}" -- ${_staged}
        COMMAND "${Python3_EXECUTABLE}" "${_support_source}/attest.py"
            --root "${CMAKE_SOURCE_DIR}" --compiler $<TARGET_FILE:beam_bootstrap>
            --sdk "${CMAKE_BINARY_DIR}/sdk.toml" --beam-directory "${RUN_OUTPUT_DIRECTORY}"
            --output "${CMAKE_BINARY_DIR}/runtime-support/attestations/${TARGET}.json"
            --resources ${RUN_RESOURCES} ${_headers} --sources ${_staged}
        DEPENDS beam_bootstrap ${_staged} ${_headers}
            "${_support_source}/attest.py"
            "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/beam_native_contracts.def"
            "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/beam_native_instructions.def"
            ${RUN_DEPENDS} "${_projection}"
            beam_runtime_projection_transform VERBATIM)
    add_custom_target(${TARGET} DEPENDS ${_outputs})
    set_target_properties(${TARGET} PROPERTIES BEAM_INVENTORY_FILES "${_outputs}")
endfunction()

set(BEAM_PROJECTED_LIBRARY_MODULES beam_ets beam_runtime_unicode_util)
set(BEAM_PROJECTED_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modern-library/ebin")
set(_library_sources)
foreach(_module IN ITEMS unicode_util ets)
    list(APPEND _library_sources "${CMAKE_SOURCE_DIR}/src/beam/support/src/${_module}.erl")
endforeach()
beam_add_runtime_component(beam_public_library_modules SOURCES ${_library_sources}
    OUTPUT_DIRECTORY "${BEAM_PROJECTED_LIBRARY_OUTPUT_DIRECTORY}"
    INCLUDE_DIRECTORIES "${BEAM_SUPPORT_INCLUDE_DIRECTORY}")
beam_add_runtime_component(beam_legacy_string_module
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/beam_runtime_string_legacy.erl"
    OUTPUT_DIRECTORY "${BEAM_RUNTIME_SUPPORT_OUTPUT_DIRECTORY}")
foreach(_value IN ITEMS MODULES OUTPUT_DIRECTORY)
    set(BEAM_PROJECTED_LIBRARY_${_value} "${BEAM_PROJECTED_LIBRARY_${_value}}" PARENT_SCOPE)
endforeach()

set(_runtime_retained
    io_lib_format io_lib_fread
    erl_anno erl_bits erl_error erl_internal erl_posix_msg
    erl_stdlib_errors eval_bits orddict
    otp_internal proplists queue sets)
set(_runtime_sources)
foreach(_module IN LISTS _runtime_retained)
    list(APPEND _runtime_sources "${CMAKE_SOURCE_DIR}/src/beam/support/src/${_module}.erl")
endforeach()
list(APPEND _runtime_sources "${CMAKE_SOURCE_DIR}/src/beam/support/generators/parser/ms_transform.erl")
# EPP resolves filed-doc attributes relative to the physical source directory.
# Preserve the declared relative resource in the staging tree as well.
set(_ms_doc "${CMAKE_BINARY_DIR}/runtime-support/doc/src/ms_transform.md")
set(_ms_source_doc "${CMAKE_SOURCE_DIR}/src/beam/support/doc/src/ms_transform.md")
add_custom_command(OUTPUT "${_ms_doc}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/runtime-support/doc/src"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_ms_source_doc}" "${_ms_doc}"
    DEPENDS "${_ms_source_doc}" VERBATIM)
beam_add_runtime_component(beam_runtime_support_modules SOURCES ${_runtime_sources}
    OUTPUT_DIRECTORY "${BEAM_RUNTIME_SUPPORT_OUTPUT_DIRECTORY}"
    INCLUDE_DIRECTORIES "${BEAM_SUPPORT_INCLUDE_DIRECTORY}"
    DEPENDS "${_ms_doc}"
    RESOURCES "${_ms_source_doc}")
foreach(_value IN ITEMS SOURCE_DIR OUTPUT_DIRECTORY)
    set(BEAM_RUNTIME_SUPPORT_${_value} "${BEAM_RUNTIME_SUPPORT_${_value}}" PARENT_SCOPE)
endforeach()
