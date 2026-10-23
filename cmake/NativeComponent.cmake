# Common private compilation context for core and distribution native code.
# Each component declares its own sources, local headers and atom-table policy.
function(beam_configure_native_component target)
    target_link_libraries(${target} PRIVATE beam-erlang::erts_headers)
    target_compile_definitions(${target} PRIVATE
        HAVE_CONFIG_H ${BEAM_PLATFORM_DEFINITIONS}
        LIBSCTP= ERLANG_ARCHITECTURE="${ERTS_HOST_TRIPLE}"
    )
    if(ERTS_BUILD_FLAVOR STREQUAL "jit")
        target_compile_definitions(${target} PRIVATE BEAMASM=1)
    endif()
    set(_emulator "${PROJECT_SOURCE_DIR}/src/erts/emulator")
    target_include_directories(${target} PRIVATE
        ${_emulator}/beam
        ${_emulator}/sys/${BEAM_SYS_DIRECTORY}
        ${_emulator}/sys/common
        ${_emulator}/../include/internal
        ${_emulator}/../include
    )
endfunction()
