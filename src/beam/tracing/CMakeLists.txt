set(BEAM_ERL_TRACER_BEAM
    "${CMAKE_CURRENT_SOURCE_DIR}/ebin/erl_tracer.beam" PARENT_SCOPE)

add_library(beam_erl_tracer_nif MODULE erl_tracer_nif.c)
beam_configure_native_component(beam_erl_tracer_nif)
target_compile_definitions(beam_erl_tracer_nif PRIVATE ERTS_CONFIG_SKIP_ATOM_TABLE)
target_link_libraries(beam_erl_tracer_nif PRIVATE libbeam)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
    set(_exports "${CMAKE_CURRENT_BINARY_DIR}/beam_erl_tracer_nif-exports.map")
    file(CONFIGURE OUTPUT "${_exports}" CONTENT "{ global: nif_init; local: *; };\n")
    target_link_options(beam_erl_tracer_nif PRIVATE "LINKER:-z,defs" "LINKER:--version-script=${_exports}")
    set_property(TARGET beam_erl_tracer_nif APPEND PROPERTY LINK_DEPENDS "${_exports}")
endif()
set_target_properties(beam_erl_tracer_nif PROPERTIES
    OUTPUT_NAME "erl_tracer"
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/priv"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${BEAM_LOADER_ORIGIN};${BEAM_LOADER_ORIGIN}/../../../.."
)

set(_ordinary "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin/erl_tracer.beam")
add_custom_command(OUTPUT "${_ordinary}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/ebin/erl_tracer.beam" "${_ordinary}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ebin/erl_tracer.beam" VERBATIM)
add_custom_target(beam_tracing_artifacts DEPENDS "${_ordinary}")
set_target_properties(beam_tracing_artifacts PROPERTIES
    BEAM_INVENTORY_MODULES erl_tracer BEAM_INVENTORY_FILES "${_ordinary}")
