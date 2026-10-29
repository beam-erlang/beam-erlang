# The BEAM virtual machine. The product targets are libbeam.so and the beam executable.

include(${CMAKE_SOURCE_DIR}/src/erts/emulator/cmake/Generators.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/CompileFlags.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/Ycf.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/EmulatorSources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/ThirdParty.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/Ethread.cmake)

if(ERTS_BUILD_FLAVOR STREQUAL "jit")
    erts_add_beam_makeops(jit)
    erts_add_make_alloc_types(jit)
    erts_add_make_tables(jit)
    erts_add_make_version(jit)
    erts_add_jit_asm_global_header(jit)
    erts_add_make_driver_tab(jit)
    set(EMULATOR_GENERATED_DIR "${CMAKE_BINARY_DIR}/emulator/jit")
    set(EMULATOR_GENERATED_SOURCES
        ${ERTS_BEAM_MAKEOPS_GENERATED_SOURCES_jit}
        ${ERTS_MAKE_TABLES_GENERATED_SOURCES_jit}
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
    erts_add_make_version(emu)
    erts_add_make_driver_tab(emu)
    set(EMULATOR_GENERATED_DIR "${CMAKE_BINARY_DIR}/emulator/emu")
    set(EMULATOR_GENERATED_SOURCES
        ${ERTS_BEAM_MAKEOPS_GENERATED_SOURCES_emu}
        ${ERTS_MAKE_TABLES_GENERATED_SOURCES_emu}
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

# Core BIFs share one implementation across all VM compositions.
add_library(beam_core_shared_data_native OBJECT
    beam/erl_bif_atomics.c
    beam/erl_bif_counters.c
    beam/erl_bif_persistent.c
)
beam_configure_native_component(beam_core_shared_data_native)

add_library(beam_emulator OBJECT)
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # Magic binary destructors are also type identities; keep their addresses
    # distinct even when their machine code is identical.
    target_compile_options(beam_emulator PRIVATE -fno-ipa-icf)
    target_compile_options(beam_core_shared_data_native PRIVATE -fno-ipa-icf)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GNU-unique inline statics pin a rejected JIT runtime after dlclose.
    # A native dependency must be releasable before any VM initialization.
    target_compile_options(beam_emulator PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-gnu-unique>")
endif()
target_sources(beam_emulator PRIVATE ${EMULATOR_GENERATED_SOURCES})

add_custom_target(emulator_generated_headers_${ERTS_BUILD_FLAVOR}
    DEPENDS ${EMULATOR_GENERATED_HEADERS}
)
add_dependencies(beam_emulator emulator_generated_headers_${ERTS_BUILD_FLAVOR})

# config.h includes erl_atom_table.h. Listing the generated headers on the
# interface target exposes them to consumers; the explicit dependency also
# orders their generation for direct component builds in other directories.
target_sources(beam_erts_headers INTERFACE ${EMULATOR_GENERATED_HEADERS})
add_dependencies(beam_erts_headers emulator_generated_headers_${ERTS_BUILD_FLAVOR})

erts_configure_compile_flags_header(beam_emulator)
erts_configure_ycf_headers(beam_emulator)
target_link_libraries(beam_emulator PRIVATE beam-erlang::erts_headers)

erts_add_emulator_sources(beam_emulator)
target_sources(beam_emulator PRIVATE
    ${CMAKE_SOURCE_DIR}/src/beam.c
    ${CMAKE_SOURCE_DIR}/src/beam_memory.c
    ${CMAKE_SOURCE_DIR}/src/beam_native.c
    ${CMAKE_SOURCE_DIR}/src/beam_zlib.c
    ${CMAKE_SOURCE_DIR}/src/beam_packet.c
    ${CMAKE_SOURCE_DIR}/src/beam_runtime_state.c
    ${CMAKE_SOURCE_DIR}/src/beam_composition.c
)
if(MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/src/beam.c" "${CMAKE_SOURCE_DIR}/src/beam_runtime_state.c"
        PROPERTIES COMPILE_OPTIONS /experimental:c11atomics)
endif()
target_include_directories(beam_emulator PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
erts_add_emulator_third_party_libraries()
erts_add_ethread_library()

erts_add_core_preload_target(beam_core_preload "${EMULATOR_GENERATED_DIR}")
target_include_directories(beam_emulator PRIVATE "${CMAKE_BINARY_DIR}/components/include")
# Object targets are direct dependencies so every native entry is retained.
function(beam_link_runtime TARGET)
    target_link_libraries(${TARGET} PRIVATE
        beam_emulator beam_core_preload beam_core_shared_data_native
        ryu pcre zlib ethread ${BEAM_SYSTEM_LIBRARIES})
    if(ERTS_TERMCAP_LIB)
        target_link_libraries(${TARGET} PRIVATE ${ERTS_TERMCAP_LIB})
    endif()
    if(ERTS_BUILD_FLAVOR STREQUAL "jit")
        set_target_properties(${TARGET} PROPERTIES LINKER_LANGUAGE CXX)
    endif()
endfunction()

# libbeam.so is the VM. beam is a thin executable that calls it.
add_library(libbeam SHARED
    ${CMAKE_SOURCE_DIR}/src/beam_runtime_image.c
    ${CMAKE_SOURCE_DIR}/src/beam_elf_image.c)
add_library(BEAM::Runtime ALIAS libbeam)
beam_link_runtime(libbeam)
target_include_directories(libbeam INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(libbeam PROPERTIES
    OUTPUT_NAME beam
    EXPORT_NAME Runtime
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
if(ERTS_BUILD_FLAVOR STREQUAL "jit")
    set_target_properties(libbeam PROPERTIES LINKER_LANGUAGE CXX)
endif()

beam_platform_library(libbeam BEAM)
if(MINGW)
    # Explicit native entry exports otherwise suppress MinGW's automatic exports.
    target_link_options(libbeam PRIVATE LINKER:--export-all-symbols)
endif()

# The archive contains the same compiled VM and support objects as libbeam.so.
# Only this private final-link marker differs between the two images.
add_library(libbeam_static STATIC
    ${CMAKE_SOURCE_DIR}/src/beam_runtime_image.c
    ${CMAKE_SOURCE_DIR}/src/beam_elf_image.c)
target_compile_definitions(libbeam_static PRIVATE BEAM_STATIC_RUNTIME=1)
foreach(_objects IN ITEMS beam_emulator beam_core_preload beam_core_shared_data_native
        ryu pcre zlib ethread)
    target_sources(libbeam_static PRIVATE $<TARGET_OBJECTS:${_objects}>)
endforeach()
set(_static_name beam)
if(WIN32)
    set(_static_name beam_static)
endif()
set_target_properties(libbeam_static PROPERTIES OUTPUT_NAME ${_static_name}
    EXPORT_NAME RuntimeStaticArchive ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
add_library(beam_runtime_static INTERFACE)
add_library(BEAM::RuntimeStatic ALIAS beam_runtime_static)
set_target_properties(beam_runtime_static PROPERTIES EXPORT_NAME RuntimeStatic)
target_include_directories(beam_runtime_static INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(beam_runtime_static INTERFACE
    "$<BUILD_INTERFACE:$<LINK_LIBRARY:WHOLE_ARCHIVE,libbeam_static>>"
    "$<INSTALL_INTERFACE:$<LINK_LIBRARY:WHOLE_ARCHIVE,BEAM::RuntimeStaticArchive>>"
    ${BEAM_SYSTEM_LIBRARIES})
if(MSVC)
    # Magic binary destructors are also runtime type identities. Identical code
    # folding can alias unrelated destructors, including ETS and counter refs.
    target_link_options(libbeam PRIVATE /OPT:NOICF)
    target_link_options(beam_runtime_static INTERFACE /OPT:NOICF)
endif()
if(ERTS_TERMCAP_LIB)
    target_link_libraries(beam_runtime_static INTERFACE ${ERTS_TERMCAP_LIB})
endif()
if(ERTS_BUILD_FLAVOR STREQUAL "jit")
    # An installed C-only project does not enable CMake's C++ linker selection.
    target_link_libraries(beam_runtime_static INTERFACE ${BEAM_CXX_RUNTIME})
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
target_link_options(beam_runtime_static INTERFACE
    "$<BUILD_INTERFACE:LINKER:--export-dynamic-symbol-list=${CMAKE_SOURCE_DIR}/cmake/RuntimeExports.list>"
    "$<INSTALL_INTERFACE:LINKER:--export-dynamic-symbol-list=$<INSTALL_PREFIX>/lib/beam/RuntimeExports.list>")
elseif(APPLE)
    # Mach-O spelling of the same public native/runtime export inventory.
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/RuntimeExports.macho"
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/runtime-exports.py"
            --inventory "${CMAKE_SOURCE_DIR}/cmake/RuntimeExports.list"
            --archive $<TARGET_FILE:libbeam_static> --nm "${CMAKE_NM}"
            --output "${CMAKE_BINARY_DIR}/RuntimeExports.macho"
        DEPENDS libbeam_static "${CMAKE_SOURCE_DIR}/cmake/RuntimeExports.list"
            "${CMAKE_SOURCE_DIR}/tools/runtime-exports.py" VERBATIM)
    add_custom_target(beam_runtime_exports ALL DEPENDS "${CMAKE_BINARY_DIR}/RuntimeExports.macho")
    add_dependencies(beam_runtime_static beam_runtime_exports)
    target_link_options(beam_runtime_static INTERFACE
        "$<BUILD_INTERFACE:LINKER:-exported_symbols_list,${CMAKE_BINARY_DIR}/RuntimeExports.macho>"
        "$<INSTALL_INTERFACE:LINKER:-exported_symbols_list,$<INSTALL_PREFIX>/lib/beam/RuntimeExports.macho>")
    install(FILES "${CMAKE_BINARY_DIR}/RuntimeExports.macho" DESTINATION lib/beam)
endif()
foreach(_pair IN ITEMS "libbeam;shared" "beam_runtime_static;static")
    list(GET _pair 0 _target)
    list(GET _pair 1 _linkage)
    set_property(TARGET ${_target} PROPERTY INTERFACE_BEAM_RUNTIME_LINKAGE ${_linkage})
    set_property(TARGET ${_target} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING BEAM_RUNTIME_LINKAGE)
endforeach()

# The VM forks this program at startup. It is not a user-facing command.
if(BEAM_HAS_CHILD_SETUP)
add_executable(erl_child_setup
    sys/unix/erl_child_setup.c
    sys/unix/sys_uds.c
    beam/hash.c
)
target_compile_definitions(erl_child_setup PRIVATE
    HAVE_CONFIG_H
    USE_THREADS
    _THREAD_SAFE
    _REENTRANT
    _GNU_SOURCE
    POSIX_THREADS
    _POSIX_THREAD_SAFE_FUNCTIONS
)
target_include_directories(erl_child_setup PRIVATE
    ${EMULATOR_GENERATED_DIR}
    ${CMAKE_BINARY_DIR}/erts
    ${CMAKE_BINARY_DIR}/erts/include
    ${CMAKE_BINARY_DIR}/erts/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/beam
    ${CMAKE_CURRENT_SOURCE_DIR}/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/sys/unix
    ${CMAKE_CURRENT_SOURCE_DIR}/sys/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/internal
)
target_link_libraries(erl_child_setup PRIVATE beam-erlang::erts_headers)
set_target_properties(erl_child_setup PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libexec/beam
)

endif()

message(STATUS "libbeam: ${CMAKE_BINARY_DIR}/lib/libbeam.so (${ERTS_BUILD_FLAVOR})")
message(STATUS "beam executable: ${CMAKE_BINARY_DIR}/bin/beam (${ERTS_BUILD_FLAVOR})")

set(_core_refresh_dir "${CMAKE_BINARY_DIR}/core-artifacts")
set(_core_refresh_outputs)
foreach(_module IN LISTS BEAM_CORE_MODULES)
    list(APPEND _core_refresh_outputs "${_core_refresh_dir}/${_module}.beam")
endforeach()
set(_core_check_stamp "${CMAKE_BINARY_DIR}/core-verification/verified.stamp")
add_custom_command(OUTPUT "${_core_check_stamp}"
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/core-verification -DVERIFY=ON
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshCoreArtifacts.cmake
    COMMAND ${CMAKE_COMMAND} -E touch "${_core_check_stamp}"
    DEPENDS beam_bootstrap
        ${BEAM_CORE_SOURCES} ${BEAM_CORE_BEAM_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/RuntimeCoreModules.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/RefreshCoreArtifacts.cmake"
    COMMENT "Verifying embedded core bytecode and inspection metadata" VERBATIM)
add_custom_target(beam_verify_core_artifacts ALL DEPENDS "${_core_check_stamp}")
add_dependencies(beam_run beam_verify_core_artifacts)
add_custom_target(beam_refresh_core_artifacts
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${_core_refresh_dir}
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshCoreArtifacts.cmake
    BYPRODUCTS ${_core_refresh_outputs}
    DEPENDS beam_bootstrap
    COMMENT "Refreshing shared core artifact candidates with inspection metadata"
    VERBATIM)
