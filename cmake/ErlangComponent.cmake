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
    set_target_properties(${TARGET} PROPERTIES BEAM_INVENTORY_FILES "${_outputs}")
endfunction()

# Producer-owned ordinary bytecode expectations, generated after compilation.
function(beam_add_bytecode_inventory target)
    cmake_parse_arguments(PARSE_ARGV 1 INV "" "NAME;INPUT_DIRECTORY;INSTALL_ROOT" "MODULES;DEPENDS")
    if(INV_UNPARSED_ARGUMENTS OR INV_KEYWORDS_MISSING_VALUES OR
       NOT INV_NAME MATCHES "^(base|boot|services)$" OR NOT INV_MODULES OR
       NOT INV_INPUT_DIRECTORY OR NOT INV_INSTALL_ROOT)
        message(FATAL_ERROR "${target}: provide inventory NAME, MODULES, INPUT_DIRECTORY and INSTALL_ROOT")
    endif()
    string(SUBSTRING "${INV_NAME}" 0 1 _initial)
    string(TOUPPER "${_initial}" _initial)
    string(SUBSTRING "${INV_NAME}" 1 -1 _rest)
    set(_output "${CMAKE_BINARY_DIR}/bytecode/include")
    set(_header "${_output}/beam_${INV_NAME}_inventory.inc")
    set(_expectations "${_output}/BEAM${_initial}${_rest}Inventory.cmake")
    set(_manifest "${CMAKE_BINARY_DIR}/${INV_INSTALL_ROOT}/modules.txt")
    set(_files)
    foreach(_module IN LISTS INV_MODULES)
        list(APPEND _files "${INV_INPUT_DIRECTORY}/${_module}.beam")
    endforeach()
    add_custom_command(OUTPUT "${_header}" "${_expectations}" "${_manifest}"
        BYPRODUCTS "${_output}/${INV_NAME}-modules.txt"
        COMMAND ${CMAKE_COMMAND}
            "-DNAME=${INV_NAME}" "-DBUILD_ID=${BEAM_COMPONENT_BUILD_ID}"
            "-DMODULES=${INV_MODULES}" "-DINPUT_DIR=${INV_INPUT_DIRECTORY}"
            "-DOUTPUT_DIR=${_output}" "-DRELATIVE_ROOT=${INV_INSTALL_ROOT}"
            -P "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_output}/${INV_NAME}-modules.txt" "${_manifest}"
        DEPENDS ${INV_DEPENDS} ${_files}
            "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
        COMMENT "Generating verified ${INV_NAME} inventory expectations" VERBATIM)
    add_custom_target(${target} DEPENDS "${_header}" "${_expectations}" "${_manifest}")
    set_target_properties(${target} PROPERTIES
        BEAM_INVENTORY_MODULES "${INV_MODULES}" BEAM_INVENTORY_FILES "${_files}"
        BEAM_INVENTORY_MANIFEST "${_manifest}" BEAM_INVENTORY_EXPECTATIONS "${_expectations}"
        BEAM_INVENTORY_HEADER "${_header}" BEAM_INVENTORY_INSTALL_ROOT "${INV_INSTALL_ROOT}")
endfunction()
