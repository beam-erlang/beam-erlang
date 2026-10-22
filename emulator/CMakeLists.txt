# The BEAM virtual machine. The product targets are libbeam.so and the beam executable.

include(${CMAKE_SOURCE_DIR}/cmake/ErlangGenerators.cmake)
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

add_library(beam_emulator OBJECT)
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
    ${CMAKE_SOURCE_DIR}/src/beam_packet.c
    ${CMAKE_SOURCE_DIR}/src/beam_services.c
    ${CMAKE_SOURCE_DIR}/src/beam_composition.c
    ${CMAKE_SOURCE_DIR}/src/beam_component.c
)
target_include_directories(beam_emulator PRIVATE ${CMAKE_SOURCE_DIR}/src)
erts_add_emulator_third_party_libraries()
erts_add_ethread_library()

erts_add_core_preload_target(beam_core_preload "${EMULATOR_GENERATED_DIR}")
include(${CMAKE_SOURCE_DIR}/cmake/ComponentBuildId.cmake)
set(BEAM_COMPONENT_BUILD_ID "${BEAM_COMPONENT_BUILD_ID}" PARENT_SCOPE)
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

# Compile returning policy in an isolated public-header target. Its object is
# included for the existing SDK convenience exports, never used by VM internals.
configure_file("${CMAKE_SOURCE_DIR}/src/beam.h" "${CMAKE_BINARY_DIR}/public-sdk/include/beam.h" COPYONLY)
add_library(beam_return_helper OBJECT ${BEAM_BOOT_NATIVE_SOURCES})
target_include_directories(beam_return_helper PRIVATE "${CMAKE_BINARY_DIR}/public-sdk/include")
target_compile_features(beam_return_helper PRIVATE c_std_11)
set_target_properties(beam_return_helper PROPERTIES POSITION_INDEPENDENT_CODE ON)

# libbeam.so is the VM. beam is a thin executable that calls it.
add_library(libbeam SHARED $<TARGET_OBJECTS:beam_return_helper>
    ${CMAKE_SOURCE_DIR}/src/beam_runtime_image.c)
add_library(BEAM::Runtime ALIAS libbeam)
beam_link_runtime(libbeam)
target_include_directories(libbeam INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
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

# The archive contains the same compiled VM and support objects as libbeam.so.
# Only this private final-link marker differs between the two images.
add_library(libbeam_static STATIC
    ${CMAKE_SOURCE_DIR}/src/beam_runtime_image.c)
target_compile_definitions(libbeam_static PRIVATE BEAM_STATIC_RUNTIME=1)
foreach(_objects IN ITEMS beam_emulator beam_core_preload beam_core_shared_data_native
        beam_return_helper ryu pcre zlib ethread)
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
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src> $<INSTALL_INTERFACE:include>)
target_link_libraries(beam_runtime_static INTERFACE
    "$<BUILD_INTERFACE:$<LINK_LIBRARY:WHOLE_ARCHIVE,libbeam_static>>"
    "$<INSTALL_INTERFACE:$<LINK_LIBRARY:WHOLE_ARCHIVE,BEAM::RuntimeStaticArchive>>"
    ${BEAM_SYSTEM_LIBRARIES})
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
    "$<INSTALL_INTERFACE:LINKER:--export-dynamic-symbol-list=$<INSTALL_PREFIX>/lib/cmake/BEAM/RuntimeExports.list>")
endif()
foreach(_pair IN ITEMS "libbeam;shared" "beam_runtime_static;static")
    list(GET _pair 0 _target)
    list(GET _pair 1 _linkage)
    set_property(TARGET ${_target} PROPERTY INTERFACE_BEAM_RUNTIME_LINKAGE ${_linkage})
    set_property(TARGET ${_target} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING BEAM_RUNTIME_LINKAGE)
endforeach()

# Distribution layout, artifact acquisition and startup recipes belong to hosts.
# The support archive uses only the public runtime API and is not linked into it.
add_library(beam_cli_recipe STATIC
    "${CMAKE_SOURCE_DIR}/src/beam_cli_recipe.c")
target_include_directories(beam_cli_recipe PRIVATE ${BEAM_RUN_INCLUDE_DIRECTORY} ${BEAM_CLI_INCLUDE_DIRECTORY} ${BEAM_INTERACTIVE_INCLUDE_DIRECTORY})
target_link_libraries(beam_cli_recipe PUBLIC BEAM::Runtime)
set(_cli_component_directory lib)
if(WIN32)
    set(_cli_component_directory bin)
endif()
target_compile_definitions(beam_cli_recipe PRIVATE
    BEAM_CLI_COMPONENT_DIRECTORY="${_cli_component_directory}"
    BEAM_CLI_LIBRARY_PREFIX="${CMAKE_SHARED_LIBRARY_PREFIX}"
    BEAM_CLI_LIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_target_properties(beam_cli_recipe PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(BEAM_BUILD_CLI)
add_executable(beam
    ${CMAKE_SOURCE_DIR}/src/beam_main.c
    ${BEAM_BOOT_CLI_SOURCES}
    ${BEAM_COMPILER_NATIVE_SOURCES})
target_include_directories(beam PRIVATE ${BEAM_COMPILER_INCLUDE_DIRECTORY})
target_link_libraries(beam PRIVATE beam_cli_recipe)
set_target_properties(beam PROPERTIES
    EXPORT_NAME CLI
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    BUILD_RPATH "${BEAM_LOADER_ORIGIN}/../lib"
    INSTALL_RPATH "${BEAM_LOADER_ORIGIN}/../lib"
    BUILD_RPATH_USE_ORIGIN TRUE
)

endif()

foreach(_compiler IN ITEMS beam_seed_compiler beam_bootstrap)
    if(TARGET ${_compiler})
        continue()
    endif()
    add_executable(${_compiler}
        "${CMAKE_SOURCE_DIR}/src/beam/compiler/beam_build_compiler.c"
        ${BEAM_COMPILER_NATIVE_SOURCES})
    target_include_directories(${_compiler} PRIVATE "${BEAM_COMPILER_INCLUDE_DIRECTORY}")
    target_link_libraries(${_compiler} PRIVATE BEAM::Runtime)
    if(_compiler STREQUAL beam_seed_compiler)
        set(_compiler_root "${BEAM_COMPILER_SEED_ROOT}")
        add_dependencies(${_compiler} beam_compiler_seed)
    else()
        set(_compiler_root "${CMAKE_BINARY_DIR}")
        add_dependencies(${_compiler} beam_compiler_tooling)
    endif()
    target_compile_definitions(${_compiler} PRIVATE
        BEAM_COMPILER_COMMAND_FUNCTION="bootstrap" BEAM_BUILD_COMPILER_ROOT="${_compiler_root}")
    set_target_properties(${_compiler} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        BUILD_RPATH "${BEAM_LOADER_ORIGIN}/../lib" BUILD_RPATH_USE_ORIGIN TRUE)
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
