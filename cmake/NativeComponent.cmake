# Common private compilation context for core and distribution native code.
# Each component declares its own sources, local headers and atom-table policy.
function(beam_configure_native_component target)
    target_link_libraries(${target} PRIVATE beam-erlang::erts_headers)
    target_compile_definitions(${target} PRIVATE
        HAVE_CONFIG_H USE_THREADS _THREAD_SAFE _REENTRANT _GNU_SOURCE
        POSIX_THREADS _POSIX_THREAD_SAFE_FUNCTIONS
        LIBSCTP= ERLANG_ARCHITECTURE="${ERTS_HOST_TRIPLE}"
    )
    if(ERTS_BUILD_FLAVOR STREQUAL "jit")
        target_compile_definitions(${target} PRIVATE BEAMASM=1)
    endif()
    set(_emulator "${PROJECT_SOURCE_DIR}/emulator")
    target_include_directories(${target} PRIVATE
        ${_emulator}/beam
        ${_emulator}/sys/unix
        ${_emulator}/sys/common
        ${_emulator}/../include/internal
        ${_emulator}/../include
    )
endfunction()
