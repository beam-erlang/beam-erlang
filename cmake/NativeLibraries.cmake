# Distribution NIFs use the standard dynamic loader. Their private native
# dependencies are built with the same runtime and remain within each family.
function(beam_configure_dynamic_nif target family output)
    beam_configure_native_component(${target})
    target_compile_definitions(${target} PRIVATE ERTS_CONFIG_SKIP_ATOM_TABLE)
    target_link_libraries(${target} PRIVATE libbeam)
    target_link_options(${target} PRIVATE "LINKER:-z,defs")
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output}"
        PREFIX ""
        C_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/runtime/${family}/priv"
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../../../.."
    )
endfunction()
