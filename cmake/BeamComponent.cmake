# Bytecode/native units are downstream of the retained OTP compiler.
function(beam_add_component TARGET)
    cmake_parse_arguments(PARSE_ARGV 1 UNIT "" "NAME;KIND" "MODULES;BEAM_FILES;NIF_ENTRIES;NATIVE_SOURCES;DEPENDS")
    if(UNIT_UNPARSED_ARGUMENTS OR NOT UNIT_NAME OR NOT UNIT_KIND)
        message(FATAL_ERROR "${TARGET}: provide component NAME and KIND")
    endif()
    list(LENGTH UNIT_MODULES _count)
    list(LENGTH UNIT_BEAM_FILES _file_count)
    list(LENGTH UNIT_NIF_ENTRIES _entry_count)
    if(NOT _count OR NOT _count EQUAL _file_count OR NOT _count EQUAL _entry_count)
        message(FATAL_ERROR "${TARGET}: module, bytecode and entry inventories disagree")
    endif()
    set(_dir "${CMAKE_BINARY_DIR}/components/${UNIT_NAME}")
    file(MAKE_DIRECTORY "${_dir}")
    set(_args)
    set(_exports "{ global: beam_component_descriptor_v1;")
    set(_macho_exports "_beam_component_descriptor_v1\n")
    math(EXPR _last "${_count} - 1")
    foreach(_index RANGE 0 ${_last})
        list(GET UNIT_MODULES ${_index} _module)
        list(GET UNIT_BEAM_FILES ${_index} _bytes)
        list(GET UNIT_NIF_ENTRIES ${_index} _nif)
        list(APPEND _args --module "${_module}" "${_bytes}" "${_nif}")
        if(NOT _nif STREQUAL "-")
            string(APPEND _exports " ${_nif};")
            string(APPEND _macho_exports "_${_nif}\n")
        endif()
    endforeach()
    string(APPEND _exports " local: *; };\n")
    file(CONFIGURE OUTPUT "${_dir}/exports.map" CONTENT "${_exports}" @ONLY)
    set(_descriptor "${_dir}/descriptor.c")
    add_custom_command(OUTPUT "${_descriptor}"
        COMMAND ${PERL_EXECUTABLE} "${CMAKE_SOURCE_DIR}/cmake/GenerateEmbeddedBytecode.pl"
            --component "${UNIT_KIND}" --output "${_descriptor}" ${_args}
        DEPENDS ${UNIT_BEAM_FILES} ${UNIT_DEPENDS}
            "${CMAKE_SOURCE_DIR}/cmake/GenerateEmbeddedBytecode.pl"
        COMMENT "Embedding ${UNIT_NAME} component bytecode" VERBATIM)
    add_library(${TARGET} SHARED ${UNIT_NATIVE_SOURCES} "${_descriptor}")
    beam_configure_native_component(${TARGET})
    target_compile_definitions(${TARGET} PRIVATE ERTS_CONFIG_SKIP_ATOM_TABLE)
    target_include_directories(${TARGET} PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/components/include")
    target_link_libraries(${TARGET} PRIVATE libbeam)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
    target_link_options(${TARGET} PRIVATE "LINKER:-z,defs" "LINKER:--version-script,${_dir}/exports.map")
    set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${_dir}/exports.map")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        file(CONFIGURE OUTPUT "${_dir}/exports.macho" CONTENT "${_macho_exports}" @ONLY)
        target_link_options(${TARGET} PRIVATE "LINKER:-exported_symbols_list,${_dir}/exports.macho")
        set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${_dir}/exports.macho")
    endif()
    set_target_properties(${TARGET} PROPERTIES
        OUTPUT_NAME "beam-${UNIT_NAME}" PREFIX "lib"
        C_VISIBILITY_PRESET default
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "${BEAM_LOADER_ORIGIN}")
    beam_platform_library(${TARGET} "BEAM${UNIT_NAME}")
endfunction()
