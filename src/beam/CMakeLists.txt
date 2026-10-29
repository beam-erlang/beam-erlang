# Each component owns its bytecode/native deployment recipe.
add_subdirectory(support)
add_subdirectory(library)
add_subdirectory(boot)
add_subdirectory(services)
add_subdirectory(file)
add_subdirectory(network)
add_subdirectory(archive)
add_subdirectory(eval)
add_subdirectory(tracing)
add_subdirectory(zlib)
add_subdirectory(compiler)
add_subdirectory(cli)
add_subdirectory(run)
add_subdirectory(interactive)

# Ordinary files outside the atomic BEAM component units.
set(BEAM_SERVICE_MODULES ${BEAM_ARCHIVE_MODULES} erl_tracer)
set(BEAM_COMPONENT_SERVICE_MODULES zlib)
# This records deployment availability, not activation: archive callers still
# select public zlib, and file-backed archives additionally select the file unit.
foreach(_required IN LISTS BEAM_ARCHIVE_REQUIRED_MODULES)
    if(NOT _required IN_LIST BEAM_SERVICE_MODULES AND NOT _required IN_LIST BEAM_COMPONENT_SERVICE_MODULES)
        message(FATAL_ERROR "Archive inventory requires public module ${_required}")
    endif()
endforeach()
foreach(_required IN LISTS BEAM_ARCHIVE_FILE_REQUIRED_MODULES)
    if(NOT _required IN_LIST BEAM_FILE_COMPONENT_MODULES)
        message(FATAL_ERROR "File-backed archive inventory requires file module ${_required}")
    endif()
endforeach()
set(BEAM_INSTALLED_MODULES)
set(BEAM_INSTALLED_BEAM_FILES)
foreach(_target IN ITEMS beam_archive_artifacts beam_tracing_artifacts)
    get_target_property(_modules ${_target} BEAM_INVENTORY_MODULES)
    get_target_property(_files ${_target} BEAM_INVENTORY_FILES)
    list(APPEND BEAM_INSTALLED_MODULES ${_modules})
    list(APPEND BEAM_INSTALLED_BEAM_FILES ${_files})
endforeach()
set(_beam_seen_modules)
foreach(_module IN LISTS BEAM_INSTALLED_MODULES BEAM_BASE_MODULES
        BEAM_BOOT_MODULES BEAM_SERVICES_MODULES BEAM_FILE_COMPONENT_MODULES BEAM_NETWORK_COMPONENT_MODULES)
    if(_module IN_LIST _beam_seen_modules OR _module IN_LIST BEAM_CORE_MODULES)
        message(FATAL_ERROR "Duplicate or core BEAM component module: ${_module}")
    endif()
    list(APPEND _beam_seen_modules "${_module}")
endforeach()

add_custom_target(beam_service_artifacts)
add_dependencies(beam_service_artifacts beam_archive_artifacts beam_tracing_artifacts)
string(JOIN "\n" _manifest ${BEAM_INSTALLED_MODULES})
set(_ordinary_root "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam")
file(MAKE_DIRECTORY "${_ordinary_root}")
file(CONFIGURE OUTPUT "${_ordinary_root}/modules.txt" CONTENT "${_manifest}\n" NEWLINE_STYLE UNIX @ONLY)
set_target_properties(beam_service_artifacts PROPERTIES
    BEAM_INVENTORY_MODULES "${BEAM_INSTALLED_MODULES}"
    BEAM_INVENTORY_FILES "${BEAM_INSTALLED_BEAM_FILES}"
    BEAM_INVENTORY_MANIFEST "${_ordinary_root}/modules.txt"
    BEAM_INVENTORY_INSTALL_ROOT lib/beam/runtime/beam)

# Distribution layout, artifact acquisition and startup recipes belong to hosts.
# The support archive uses only the public runtime API and is not linked into it.
add_library(beam_cli_recipe STATIC
    "${CMAKE_SOURCE_DIR}/src/beam_cli_recipe.c")
target_include_directories(beam_cli_recipe PRIVATE ${BEAM_RUN_INCLUDE_DIRECTORY} ${BEAM_CLI_INCLUDE_DIRECTORY} ${BEAM_INTERACTIVE_INCLUDE_DIRECTORY})
target_link_libraries(beam_cli_recipe PUBLIC BEAM::Host BEAM::Runtime)
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
target_include_directories(beam PRIVATE "${CMAKE_SOURCE_DIR}/src" ${BEAM_COMPILER_INCLUDE_DIRECTORY})
target_link_libraries(beam PRIVATE beam_cli_recipe)
set_target_properties(beam PROPERTIES
    EXPORT_NAME CLI
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    BUILD_RPATH "${BEAM_LOADER_ORIGIN}/../lib"
    INSTALL_RPATH "${BEAM_LOADER_ORIGIN}/../lib"
    BUILD_RPATH_USE_ORIGIN TRUE
)
if(MINGW)
    target_link_options(beam PRIVATE -municode)
endif()

endif()


if(BEAM_BUILD_CLI)
    add_executable(beam_launch "${CMAKE_SOURCE_DIR}/src/beam_launcher.c")
    set_target_properties(beam_launch PROPERTIES OUTPUT_NAME beam-launch)
    if(MINGW)
        target_link_options(beam_launch PRIVATE -municode)
    endif()
endif()

set(_bytecode_include "${CMAKE_BINARY_DIR}/bytecode/include")
# Support sources are downstream of verified ordinary bytecode, never VM inputs.
set(_sdk_inventories "${_bytecode_include}/inventories.c")
add_custom_command(OUTPUT "${_sdk_inventories}"
    COMMAND ${CMAKE_COMMAND} "-DINPUT_DIR=${_bytecode_include}" "-DOUTPUT=${_sdk_inventories}"
        -P "${CMAKE_SOURCE_DIR}/cmake/GenerateSDKInventories.cmake"
    DEPENDS beam_base_inventory beam_boot_inventory beam_services_inventory
        beam_startup_inventory beam_host_inventory beam_output_inventory beam_code_inventory
        "${_bytecode_include}/beam_base_inventory.inc"
        "${_bytecode_include}/beam_boot_inventory.inc"
        "${_bytecode_include}/beam_services_inventory.inc"
        "${_bytecode_include}/beam_startup_inventory.inc"
        "${_bytecode_include}/beam_host_inventory.inc"
        "${_bytecode_include}/beam_output_inventory.inc"
        "${_bytecode_include}/beam_code_inventory.inc"
        "${CMAKE_SOURCE_DIR}/cmake/GenerateSDKInventories.cmake" VERBATIM)
add_library(beam_sdk_support INTERFACE)
add_library(BEAM::SDKSupport ALIAS beam_sdk_support)
set_target_properties(beam_sdk_support PROPERTIES EXPORT_NAME SDKSupport
    BEAM_SDK_INVENTORY_SOURCE "${_sdk_inventories}")
target_sources(beam_sdk_support INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/beam_bytecode_verify.c>"
    "$<BUILD_INTERFACE:${_sdk_inventories}>"
    "$<INSTALL_INTERFACE:share/beam/sdk/beam_bytecode_verify.c>"
    "$<INSTALL_INTERFACE:share/beam/sdk/inventories.c>")
target_include_directories(beam_sdk_support INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:include>")
target_compile_features(beam_sdk_support INTERFACE c_std_11)
add_custom_target(beam_sdk_inventory_source ALL DEPENDS "${_sdk_inventories}")
add_dependencies(beam_sdk_support beam_sdk_inventory_source)
target_link_libraries(beam_cli_recipe PRIVATE BEAM::SDKSupport)


add_custom_target(beam_run ALL)
add_dependencies(beam_run beam_host_artifact beam_service_artifacts beam_compiler_tooling
    beam_cli_support beam_run_support beam_interactive_support
    beam_file_component beam_network_component beam_zlib_component beam_erl_tracer_nif
    beam_verify_base_artifacts beam_base_inventory beam_boot_inventory beam_services_inventory)
add_dependencies(beam_cli_recipe beam_base_inventory beam_boot_inventory beam_services_inventory)

add_custom_target(beam_refresh_native_artifacts
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/native-artifacts/beam
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshNativeArtifacts.cmake
    DEPENDS beam_run VERBATIM)
