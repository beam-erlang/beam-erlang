# The modern compiler and components build from attested BEAM compiler seeds.
set(_beam_host_beam "${CMAKE_BINARY_DIR}/lib/beam/ebin/beam_host.beam")

# One compiler output also supplies the explicitly loadable host bridge asset.
add_custom_command(OUTPUT "${_beam_host_beam}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib/beam/ebin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEAM_HOST_BEAM}" "${_beam_host_beam}"
    DEPENDS "${BEAM_HOST_BEAM}" beam_services_modules
    COMMENT "Staging the optional host bridge" VERBATIM)
add_custom_target(beam_host_artifact DEPENDS "${_beam_host_beam}")

set(_beam_seed_outputs)
foreach(_module IN LISTS BEAM_SERVICE_SEED_MODULES)
    list(APPEND _beam_seed_outputs
        "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin/${_module}.beam")
endforeach()
add_custom_command(
    OUTPUT ${_beam_seed_outputs}
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BEAM_SERVICE_SEED_FILES}
        "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin"
    DEPENDS ${BEAM_SERVICE_SEED_FILES}
    COMMENT "Staging BEAM component artifacts"
    VERBATIM)
add_custom_target(beam_service_artifacts DEPENDS ${_beam_seed_outputs})
add_custom_target(beam_run ALL)
add_dependencies(beam_run beam_host_artifact
    beam_service_artifacts beam_pkg_support beam_compiler_tooling beam_cli_support beam_run_support beam_interactive_support ${BEAM_COMPONENT_TARGETS}
    ${BEAM_DYNAMIC_NIF_TARGETS} ${BEAM_DYNAMIC_DRIVER_TARGETS})
set(_base_check_stamp "${CMAKE_BINARY_DIR}/base-verification/verified.stamp")
set(_base_ebin "${CMAKE_BINARY_DIR}/lib/beam/library/ebin")
set(_base_outputs)
foreach(_module IN LISTS BEAM_BASE_MODULES)
    list(APPEND _base_outputs "${_base_ebin}/${_module}.beam")
endforeach()
add_custom_command(OUTPUT "${_base_check_stamp}"
    BYPRODUCTS ${_base_outputs}
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${_base_ebin} -DVERIFY=ON
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/base-verification"
    COMMAND ${CMAKE_COMMAND} -E touch "${_base_check_stamp}"
    DEPENDS beam_bootstrap
        ${BEAM_BASE_SOURCES} ${BEAM_BASE_SEED_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake"
    COMMENT "Compiling and verifying ordinary base bytecode" VERBATIM)
add_custom_target(beam_verify_base_artifacts ALL DEPENDS "${_base_check_stamp}")
add_dependencies(beam_run beam_verify_base_artifacts)

# Expectations follow compilation; neither these tables nor their verifier
# become inputs to libbeam or its bootstrap compiler.
set(_bytecode_include "${CMAKE_BINARY_DIR}/bytecode/include")
set(_base_header "${_bytecode_include}/beam_base_inventory.h")
set(_base_expectations "${_bytecode_include}/BEAMBaseInventory.cmake")
set(_base_manifest "${CMAKE_BINARY_DIR}/lib/beam/library/modules.txt")
add_custom_command(OUTPUT "${_base_header}" "${_base_expectations}" "${_base_manifest}"
    BYPRODUCTS "${_bytecode_include}/base-modules.txt"
    COMMAND ${CMAKE_COMMAND}
        -DNAME=base "-DBUILD_ID=${BEAM_COMPONENT_BUILD_ID}" "-DMODULES=${BEAM_BASE_MODULES}"
        "-DINPUT_DIR=${_base_ebin}" "-DOUTPUT_DIR=${_bytecode_include}"
        -DRELATIVE_ROOT=lib/beam/library
        -P "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_bytecode_include}/base-modules.txt" "${_base_manifest}"
    DEPENDS "${_base_check_stamp}" ${_base_outputs}
        "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
    COMMENT "Generating verified base inventory expectations" VERBATIM)
add_custom_target(beam_base_inventory DEPENDS "${_base_header}" "${_base_expectations}" "${_base_manifest}")
add_dependencies(beam_run beam_base_inventory)
add_dependencies(beam_cli_recipe beam_base_inventory)
target_sources(beam_cli_recipe PRIVATE "${CMAKE_SOURCE_DIR}/src/beam_bytecode_verify.c")
target_include_directories(beam_cli_recipe PRIVATE "${_bytecode_include}")
# Ordinary support expectations are downstream of the verified compiler.
foreach(_inventory IN ITEMS boot services)
    string(TOUPPER "${_inventory}" _upper)
    string(SUBSTRING "${_inventory}" 0 1 _initial)
    string(TOUPPER "${_initial}" _initial)
    string(SUBSTRING "${_inventory}" 1 -1 _rest)
    set(_root "${CMAKE_BINARY_DIR}/lib/beam/${_inventory}")
    set(_header "${_bytecode_include}/beam_${_inventory}_inventory.h")
    set(_expectations "${_bytecode_include}/BEAM${_initial}${_rest}Inventory.cmake")
    set(_manifest "${_root}/modules.txt")
    add_custom_command(OUTPUT "${_header}" "${_expectations}" "${_manifest}"
        BYPRODUCTS "${_bytecode_include}/${_inventory}-modules.txt"
        COMMAND ${CMAKE_COMMAND}
            -DNAME=${_inventory} "-DBUILD_ID=${BEAM_COMPONENT_BUILD_ID}"
            "-DMODULES=${BEAM_${_upper}_MODULES}" "-DINPUT_DIR=${_root}/ebin"
            "-DOUTPUT_DIR=${_bytecode_include}" -DRELATIVE_ROOT=lib/beam/${_inventory}
            -P "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_bytecode_include}/${_inventory}-modules.txt" "${_manifest}"
        DEPENDS beam_${_inventory}_modules ${BEAM_${_upper}_BEAM_FILES}
            "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
        COMMENT "Generating verified ${_inventory} inventory expectations" VERBATIM)
    add_custom_target(beam_${_inventory}_inventory DEPENDS "${_header}" "${_expectations}" "${_manifest}")
    add_dependencies(beam_run beam_${_inventory}_inventory)
    add_dependencies(beam_cli_recipe beam_${_inventory}_inventory)
    install(FILES "${_header}" DESTINATION include)
    install(FILES "${_expectations}" DESTINATION lib/cmake/BEAM)
    install(FILES "${_manifest}" DESTINATION lib/beam/${_inventory})
    install(FILES ${BEAM_${_upper}_BEAM_FILES} DESTINATION lib/beam/${_inventory}/ebin)
endforeach()

add_custom_target(beam_refresh_base_artifacts
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/base-artifacts
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake
    DEPENDS beam_bootstrap
    COMMENT "Refreshing base component artifact candidates" VERBATIM)

string(JOIN "\n" _manifest ${BEAM_INSTALLED_MODULES})
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/modules.txt"
    CONTENT "${_manifest}\n" @ONLY)

add_custom_target(beam_refresh_native_artifacts
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/native-artifacts/beam
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshNativeArtifacts.cmake
    DEPENDS beam_run VERBATIM)
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
add_custom_target(beam_refresh_boot_artifacts)
add_dependencies(beam_refresh_boot_artifacts
    beam_refresh_native_artifacts beam_refresh_core_artifacts beam_refresh_base_artifacts)

install(TARGETS erl_child_setup RUNTIME DESTINATION libexec/beam)
install(TARGETS libbeam EXPORT BEAMSharedTargets
    RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(TARGETS libbeam_static beam_runtime_static EXPORT BEAMStaticTargets ARCHIVE DESTINATION lib)
install(FILES "${CMAKE_SOURCE_DIR}/cmake/RuntimeExports.list" DESTINATION lib/cmake/BEAM)
install(TARGETS beam EXPORT BEAMCLITargets RUNTIME DESTINATION bin)
install(CODE "file(CREATE_LINK beam \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/beam-pkg\" SYMBOLIC)")
install(FILES "${CMAKE_SOURCE_DIR}/src/beam.h"
    "${CMAKE_SOURCE_DIR}/src/beam_bytecode.h" "${_base_header}" DESTINATION include)
install(FILES "${CMAKE_SOURCE_DIR}/src/beam_bytecode_verify.c" DESTINATION share/beam/sdk)
install(FILES "${_base_expectations}" "${CMAKE_SOURCE_DIR}/cmake/BEAMBytecode.cmake"
    DESTINATION lib/cmake/BEAM)
install(FILES ${_base_outputs} DESTINATION lib/beam/library/ebin)
install(FILES "${_base_manifest}" DESTINATION lib/beam/library)
install(FILES "${_beam_host_beam}" DESTINATION lib/beam/ebin)
install(TARGETS ${BEAM_COMPONENT_TARGETS} LIBRARY DESTINATION lib)
install(FILES ${BEAM_INSTALLED_BEAM_FILES} DESTINATION lib/beam/runtime/beam/ebin)
install(FILES "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/modules.txt" DESTINATION lib/beam/runtime/beam)
install(TARGETS ${BEAM_NATIVE_NIF_TARGETS}
    LIBRARY DESTINATION lib/beam/runtime/beam/priv)

# Public native-extension inputs; symbols resolve in the running VM.
add_library(beam_nif_sdk INTERFACE)
set_target_properties(beam_nif_sdk PROPERTIES EXPORT_NAME NIF)
target_include_directories(beam_nif_sdk INTERFACE $<INSTALL_INTERFACE:include/beam/nif>)
install(TARGETS beam_nif_sdk EXPORT BEAMTargets)
add_library(beam_driver_sdk INTERFACE)
set_target_properties(beam_driver_sdk PROPERTIES EXPORT_NAME Driver)
target_include_directories(beam_driver_sdk INTERFACE $<INSTALL_INTERFACE:include/beam/nif>)
install(TARGETS beam_driver_sdk EXPORT BEAMTargets)
install(FILES
    "${CMAKE_SOURCE_DIR}/src/beam_native.h"
    "${CMAKE_SOURCE_DIR}/emulator/beam/erl_nif.h"
    "${CMAKE_SOURCE_DIR}/emulator/beam/erl_driver.h"
    "${CMAKE_SOURCE_DIR}/emulator/beam/erl_drv_nif.h"
    "${CMAKE_SOURCE_DIR}/emulator/beam/erl_nif_api_funcs.h"
    "${CMAKE_BINARY_DIR}/erts/include/erl_int_sizes_config.h"
    DESTINATION include/beam/nif)
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/sdk.toml" CONTENT
"format = 1
identity = \"${BEAM_RELEASE_VERSION}\"
build-id = \"${BEAM_COMPONENT_BUILD_ID}\"
os = \"${CMAKE_SYSTEM_NAME}\"
architecture = \"${CMAKE_SYSTEM_PROCESSOR}\"
triple = \"${ERTS_HOST_TRIPLE}\"
flavor = \"${ERTS_BUILD_FLAVOR}\"
" @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/sdk.toml" DESTINATION lib/beam)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/BEAMConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/BEAMConfig.cmake"
    INSTALL_DESTINATION lib/cmake/BEAM)
# The runtime release number is not a promise of C API compatibility.
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/BEAMConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(EXPORT BEAMTargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
install(EXPORT BEAMSharedTargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
install(EXPORT BEAMStaticTargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
install(EXPORT BEAMCLITargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
install(FILES
    "${CMAKE_BINARY_DIR}/BEAMConfig.cmake"
    "${CMAKE_BINARY_DIR}/BEAMConfigVersion.cmake"
    DESTINATION lib/cmake/BEAM)

install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMDeployment.cmake" DESTINATION lib/cmake/BEAM)

install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMNif.cmake" DESTINATION lib/cmake/BEAM)

# VM, public native headers, generic process helper and core inspection bytes.
set(BEAM_RUNTIME_ARTIFACT_RECORDS
    "runtime|runtime|lib/libbeam.so|-"
    "runtime.static|runtime-static|lib/libbeam.a|link.static,link.static.configuration,link.static.exports"
    "link.static|link-support|lib/cmake/BEAM/BEAMStaticTargets.cmake|-"
    "link.static.configuration|link-support|lib/cmake/BEAM/BEAMStaticTargets-%CONFIG%.cmake|-"
    "link.static.exports|link-support|lib/cmake/BEAM/RuntimeExports.list|-"
    "helper.child_setup|executable-helper|libexec/beam/erl_child_setup|-")
list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
    "header.beam_native|native-header|include/beam/nif/beam_native.h|-"
    "header.erl_nif|native-header|include/beam/nif/erl_nif.h|header.erl_drv_nif,header.erl_nif_api_funcs"
    "header.erl_driver|native-header|include/beam/nif/erl_driver.h|header.erl_drv_nif"
    "header.erl_drv_nif|native-header|include/beam/nif/erl_drv_nif.h|header.erl_int_sizes_config"
    "header.erl_nif_api_funcs|native-header|include/beam/nif/erl_nif_api_funcs.h|-"
    "header.erl_int_sizes_config|native-header|include/beam/nif/erl_int_sizes_config.h|-")
# Export the exact inputs embedded into libbeam for distribution tools that
# inspect preloaded code on disk. These files never extend runtime composition.
install(FILES ${BEAM_CORE_BEAM_FILES} DESTINATION lib/beam/core/ebin)
install(FILES ${BEAM_CORE_SOURCES} DESTINATION lib/beam/core/src)
foreach(_module IN LISTS BEAM_CORE_MODULES)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "core.${_module}|core-module|lib/beam/core/ebin/${_module}.beam|-"
        "core-source.${_module}|core-source|lib/beam/core/src/${_module}.erl|-")
endforeach()
configure_file("${CMAKE_SOURCE_DIR}/cmake/InstallRuntimeArtifacts.cmake.in"
    "${CMAKE_BINARY_DIR}/InstallRuntimeArtifacts.cmake" @ONLY)
install(SCRIPT "${CMAKE_BINARY_DIR}/InstallRuntimeArtifacts.cmake")
install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMRuntimeArtifacts.cmake" DESTINATION lib/cmake/BEAM)
