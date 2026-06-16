# Distribution NIFs use the standard dynamic loader. Their private native
# dependencies are built with the same runtime and remain within each family.
function(beam_configure_dynamic_nif target family output)
    beam_configure_native_component(${target})
    target_compile_definitions(${target} PRIVATE ERTS_CONFIG_SKIP_ATOM_TABLE)
    target_link_libraries(${target} PRIVATE libbeam)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
        set(_exports "${CMAKE_CURRENT_BINARY_DIR}/${target}-exports.map")
        file(CONFIGURE OUTPUT "${_exports}" CONTENT "{ global: nif_init; local: *; };\n")
        target_link_options(${target} PRIVATE "LINKER:-z,defs" "LINKER:--version-script=${_exports}")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_exports}")
    endif()
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output}"
        PREFIX ""
        C_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/runtime/${family}/priv"
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${BEAM_LOADER_ORIGIN};${BEAM_LOADER_ORIGIN}/../../../.."
    )
endfunction()
