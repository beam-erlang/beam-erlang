# The BEAM virtual machine. The product target is the beam executable.

include(${CMAKE_CURRENT_LIST_DIR}/cmake/ErlangGenerators.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/CompileFlags.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/Ycf.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/EmulatorSources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/ThirdParty.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/Ethread.cmake)

if(ERTS_BUILD_FLAVOR STREQUAL "jit")
    erts_add_beam_makeops(jit)
    erts_add_make_alloc_types(jit)
    erts_add_make_tables(jit)
    erts_add_make_preload(jit)
    erts_add_make_version(jit)
    erts_add_jit_asm_global_header(jit)
    erts_add_make_driver_tab(jit)
    set(EMULATOR_GENERATED_DIR "${CMAKE_BINARY_DIR}/emulator/jit")
    set(EMULATOR_GENERATED_SOURCES
        ${ERTS_BEAM_MAKEOPS_GENERATED_SOURCES_jit}
        ${ERTS_MAKE_TABLES_GENERATED_SOURCES_jit}
        ${ERTS_PRELOAD_GENERATED_SOURCES_jit}
        ${ERTS_DRIVER_TAB_GENERATED_SOURCES_jit}
    )
    set(EMULATOR_GENERATED_HEADERS
        ${ERTS_BEAM_MAKEOPS_GENERATED_HEADERS_jit}
        ${ERTS_ALLOC_TYPES_GENERATED_HEADERS_jit}
        ${ERTS_MAKE_TABLES_GENERATED_HEADERS_jit}
        ${ERTS_VERSION_GENERATED_HEADERS_jit}
        ${ERTS_JIT_ASM_GLOBAL_GENERATED_HEADERS_jit}
    )
elseif(ERTS_BUILD_FLAVOR STREQUAL "emu")
    erts_add_beam_makeops(emu)
    erts_add_make_alloc_types(emu)
    erts_add_make_tables(emu)
    erts_add_make_preload(emu)
    erts_add_make_version(emu)
    erts_add_make_driver_tab(emu)
    set(EMULATOR_GENERATED_DIR "${CMAKE_BINARY_DIR}/emulator/emu")
    set(EMULATOR_GENERATED_SOURCES
        ${ERTS_BEAM_MAKEOPS_GENERATED_SOURCES_emu}
        ${ERTS_MAKE_TABLES_GENERATED_SOURCES_emu}
        ${ERTS_PRELOAD_GENERATED_SOURCES_emu}
        ${ERTS_DRIVER_TAB_GENERATED_SOURCES_emu}
    )
    set(EMULATOR_GENERATED_HEADERS
        ${ERTS_BEAM_MAKEOPS_GENERATED_HEADERS_emu}
        ${ERTS_ALLOC_TYPES_GENERATED_HEADERS_emu}
        ${ERTS_MAKE_TABLES_GENERATED_HEADERS_emu}
        ${ERTS_VERSION_GENERATED_HEADERS_emu}
    )
else()
    message(FATAL_ERROR "Unsupported ERTS_BUILD_FLAVOR: ${ERTS_BUILD_FLAVOR}")
endif()

target_include_directories(beam_erts_headers INTERFACE
    ${EMULATOR_GENERATED_DIR}
)

add_library(beam_emulator OBJECT)
target_sources(beam_emulator PRIVATE ${EMULATOR_GENERATED_SOURCES})

add_custom_target(emulator_generated_headers_${ERTS_BUILD_FLAVOR}
    DEPENDS ${EMULATOR_GENERATED_HEADERS}
)
add_dependencies(beam_emulator emulator_generated_headers_${ERTS_BUILD_FLAVOR})

# Conservatively order internal consumers after the generated emulator headers.
# Config itself only assembles the configured thread macro fragment.
target_sources(beam_erts_headers INTERFACE ${EMULATOR_GENERATED_HEADERS})

erts_configure_compile_flags_header(beam_emulator)
erts_configure_ycf_headers(beam_emulator)
target_link_libraries(beam_emulator PRIVATE beam-erlang::erts_headers)

erts_add_emulator_sources(beam_emulator)
erts_add_emulator_third_party_libraries()
erts_add_ethread_library()

set(_erts_beam_libs beam_emulator ryu pcre zstd zlib ethread)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND _erts_beam_libs ws2_32 mswsock iphlpapi kernel32 advapi32 shell32 user32)
else()
    list(APPEND _erts_beam_libs Threads::Threads m ${CMAKE_DL_LIBS})
endif()
if(ERTS_TERMCAP_LIB)
    list(APPEND _erts_beam_libs ${ERTS_TERMCAP_LIB})
endif()

add_executable(beam)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_erts_preload_rc "${ERTS_PRELOAD_RC_${ERTS_BUILD_FLAVOR}}")
    target_sources(beam PRIVATE
        sys/win32/win32_beam_main.c
        ${_erts_preload_rc}
    )
    set_source_files_properties(${_erts_preload_rc} PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32"
    )
    add_dependencies(beam preload_${ERTS_BUILD_FLAVOR})
endif()
target_link_libraries(beam PRIVATE ${_erts_beam_libs})
set_target_properties(beam PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Copies, not symlinks. Developer Mode is not required.
    add_custom_command(TARGET beam POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:beam>
            ${CMAKE_BINARY_DIR}/bin/beam.smp.exe
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:beam>
            ${CMAKE_BINARY_DIR}/bin/beam.${ERTS_BUILD_FLAVOR}.exe
        COMMENT "Publishing beam.smp.exe and beam.${ERTS_BUILD_FLAVOR}.exe"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(TARGET beam POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_PDB_FILE:beam>
                ${CMAKE_BINARY_DIR}/bin/beam.smp.pdb
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_PDB_FILE:beam>
                ${CMAKE_BINARY_DIR}/bin/beam.${ERTS_BUILD_FLAVOR}.pdb
            COMMENT "Publishing flavor program databases"
        )
    endif()
else()
    add_custom_command(TARGET beam POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            $<TARGET_FILE_NAME:beam>
            ${CMAKE_BINARY_DIR}/bin/beam.smp
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            $<TARGET_FILE_NAME:beam>
            ${CMAKE_BINARY_DIR}/bin/beam.${ERTS_BUILD_FLAVOR}
        COMMENT "Publishing beam.smp and beam.${ERTS_BUILD_FLAVOR}"
    )

    # The forker helper is required during VM initialization, even without ports.
    # Keep it independent of the emulator object library and its main()/globals.
    # Windows creates child processes with CreateProcess inside sys/win32/sys.c.
    add_executable(erl_child_setup
        sys/unix/erl_child_setup.c
        sys/unix/sys_uds.c
        beam/hash.c
    )
    get_target_property(_erts_emulator_includes beam_emulator INCLUDE_DIRECTORIES)
    target_include_directories(erl_child_setup PRIVATE ${_erts_emulator_includes})
    target_compile_definitions(erl_child_setup PRIVATE
        HAVE_CONFIG_H USE_THREADS
        _THREAD_SAFE _REENTRANT POSIX_THREADS _POSIX_THREAD_SAFE_FUNCTIONS
        ERLANG_ARCHITECTURE="${ERTS_HOST_TRIPLE}"
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(erl_child_setup PRIVATE _GNU_SOURCE)
    endif()
    target_link_libraries(erl_child_setup PRIVATE beam-erlang::erts_headers Threads::Threads)
    set_target_properties(erl_child_setup PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # sys_drivers.c requests the suffixed helper in a DEBUG emulator. Keep the
    # existing executable name and publish a relative alias for that lookup.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(TARGET erl_child_setup POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E create_symlink
                $<TARGET_FILE_NAME:erl_child_setup>
                ${CMAKE_BINARY_DIR}/bin/erl_child_setup.debug
            BYPRODUCTS ${CMAKE_BINARY_DIR}/bin/erl_child_setup.debug
            COMMENT "Publishing erl_child_setup.debug")
    else()
        file(REMOVE ${CMAKE_BINARY_DIR}/bin/erl_child_setup.debug)
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(ERTS_WINDOWS_INSTALL 1)
    install(TARGETS beam RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Flavor PDB copies in InstallAliases.cmake read this installed file.
        install(FILES $<TARGET_PDB_FILE:beam>
            DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
else()
    set(ERTS_WINDOWS_INSTALL 0)
    install(TARGETS beam erl_child_setup RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake/InstallAliases.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/InstallAliases.cmake @ONLY)
install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/InstallAliases.cmake)

message(STATUS "beam executable: ${CMAKE_BINARY_DIR}/bin/beam (${ERTS_BUILD_FLAVOR})")
