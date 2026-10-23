if(TARGET erl_child_setup)
    install(TARGETS erl_child_setup RUNTIME DESTINATION libexec/beam)
endif()
install(TARGETS libbeam EXPORT BEAMSharedTargets
    RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib FRAMEWORK DESTINATION lib)
install(TARGETS libbeam_static beam_runtime_static EXPORT BEAMStaticTargets ARCHIVE DESTINATION lib)
install(FILES "${CMAKE_SOURCE_DIR}/cmake/RuntimeExports.list" DESTINATION lib/cmake/BEAM)
if(BEAM_BUILD_CLI)
    install(TARGETS beam EXPORT BEAMCLITargets RUNTIME DESTINATION bin)
    add_executable(beam_launch "${CMAKE_SOURCE_DIR}/src/beam_launcher.c")
    set_target_properties(beam_launch PROPERTIES OUTPUT_NAME beam-launch)
    if(MINGW)
        target_link_options(beam_launch PRIVATE -municode)
    endif()
    install(TARGETS beam_launch RUNTIME DESTINATION bin)
endif()
install(FILES "${CMAKE_SOURCE_DIR}/src/beam.h"
    "${CMAKE_SOURCE_DIR}/src/beam_bytecode.h" "${_base_header}" DESTINATION include)
install(FILES "${CMAKE_SOURCE_DIR}/src/beam_bytecode_verify.c" DESTINATION share/beam/sdk)
install(FILES "${_base_expectations}" "${CMAKE_SOURCE_DIR}/cmake/BEAMBytecode.cmake"
    DESTINATION lib/cmake/BEAM)
install(FILES ${_base_outputs} DESTINATION lib/beam/library/ebin)
install(FILES "${_base_manifest}" DESTINATION lib/beam/library)
install(FILES "${_beam_host_beam}" DESTINATION lib/beam/ebin)
install(TARGETS ${BEAM_COMPONENT_TARGETS} LIBRARY DESTINATION lib RUNTIME DESTINATION bin FRAMEWORK DESTINATION lib)
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
    "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_nif.h"
    "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_driver.h"
    "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_drv_nif.h"
    "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_nif_api_funcs.h"
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
include("${CMAKE_SOURCE_DIR}/cmake/SDKTarget.cmake")

set(BEAM_RUNTIME_RELATIVE "lib/${CMAKE_SHARED_LIBRARY_PREFIX}beam${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(BEAM_STATIC_RELATIVE "lib/${CMAKE_STATIC_LIBRARY_PREFIX}beam${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(WIN32)
    set(BEAM_RUNTIME_RELATIVE "bin/${CMAKE_SHARED_LIBRARY_PREFIX}beam${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(BEAM_STATIC_RELATIVE "lib/${CMAKE_STATIC_LIBRARY_PREFIX}beam_static${CMAKE_STATIC_LIBRARY_SUFFIX}")
elseif(BEAM_APPLE_FRAMEWORKS)
    set(BEAM_RUNTIME_RELATIVE "lib/BEAM.framework/BEAM")
endif()
set(BEAM_COMPONENT_INSTALL_DIR lib)
if(WIN32)
    set(BEAM_COMPONENT_INSTALL_DIR bin)
endif()
set(BEAM_FILE_RELATIVE "${BEAM_COMPONENT_INSTALL_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}beam-file${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(BEAM_NETWORK_RELATIVE "${BEAM_COMPONENT_INSTALL_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}beam-network${CMAKE_SHARED_LIBRARY_SUFFIX}")
if(BEAM_APPLE_FRAMEWORKS)
    set(BEAM_FILE_RELATIVE lib/BEAMfile.framework/BEAMfile)
    set(BEAM_NETWORK_RELATIVE lib/BEAMnetwork.framework/BEAMnetwork)
endif()
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
if(BEAM_BUILD_CLI)
    install(EXPORT BEAMCLITargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
endif()
install(FILES
    "${CMAKE_BINARY_DIR}/BEAMConfig.cmake"
    "${CMAKE_BINARY_DIR}/BEAMConfigVersion.cmake"
    DESTINATION lib/cmake/BEAM)

install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMDeployment.cmake" DESTINATION lib/cmake/BEAM)

install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMNif.cmake" DESTINATION lib/cmake/BEAM)

# VM, public native headers, generic process helper and core inspection bytes.
set(BEAM_RUNTIME_ARTIFACT_RECORDS
    "sdk.target|target-metadata|lib/beam/target.toml|compiler.generation"
    "compiler.generation|compiler-provenance|lib/beam/compiler-generation.json|-"
    "runtime|runtime|${BEAM_RUNTIME_RELATIVE}|-"
    "runtime.static|runtime-static|${BEAM_STATIC_RELATIVE}|link.static,link.static.configuration,link.static.exports"
    "link.static|link-support|lib/cmake/BEAM/BEAMStaticTargets.cmake|-"
    "link.static.configuration|link-support|lib/cmake/BEAM/BEAMStaticTargets-%CONFIG%.cmake|-"
    "link.static.exports|link-support|lib/cmake/BEAM/RuntimeExports.list|-"
)
if(BEAM_BUILD_CLI)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "command.beam|executable-helper|bin/beam${CMAKE_EXECUTABLE_SUFFIX}|runtime"
        "command.launcher|executable-helper|bin/beam-launch${CMAKE_EXECUTABLE_SUFFIX}|-")
endif()
if(TARGET erl_child_setup)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS "helper.child_setup|executable-helper|libexec/beam/erl_child_setup|-")
endif()
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
foreach(_component IN ITEMS file network zlib)
    set(_relative "${BEAM_COMPONENT_INSTALL_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}beam-${_component}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(BEAM_APPLE_FRAMEWORKS)
        set(_relative "lib/BEAM${_component}.framework/BEAM${_component}")
    endif()
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS "component.${_component}|native-component|${_relative}|runtime")
endforeach()
if(ANDROID)
    find_file(BEAM_ANDROID_CXX_RUNTIME libc++_shared.so
        PATHS "${CMAKE_SYSROOT}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
              "${CMAKE_SYSROOT}/usr/lib/${ANDROID_TOOLCHAIN_NAME}"
        NO_DEFAULT_PATH REQUIRED)
    install(FILES "${BEAM_ANDROID_CXX_RUNTIME}" DESTINATION lib)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "runtime.cxx|runtime-dependency|lib/libc++_shared.so|-")
    list(REMOVE_ITEM BEAM_RUNTIME_ARTIFACT_RECORDS "runtime|runtime|${BEAM_RUNTIME_RELATIVE}|-")
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS "runtime|runtime|${BEAM_RUNTIME_RELATIVE}|runtime.cxx")
endif()
configure_file("${CMAKE_SOURCE_DIR}/cmake/InstallRuntimeArtifacts.cmake.in"
    "${CMAKE_BINARY_DIR}/InstallRuntimeArtifacts.cmake" @ONLY)
install(SCRIPT "${CMAKE_BINARY_DIR}/InstallRuntimeArtifacts.cmake")
install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMRuntimeArtifacts.cmake" DESTINATION lib/cmake/BEAM)

if(MSVC AND BEAM_RELEASE_DEBUG_INFO)
    foreach(_debug_target IN ITEMS libbeam beam ${BEAM_COMPONENT_TARGETS} ${BEAM_NATIVE_NIF_TARGETS})
        if(TARGET ${_debug_target})
            install(FILES "$<TARGET_PDB_FILE:${_debug_target}>" DESTINATION bin OPTIONAL)
        endif()
    endforeach()
endif()

install(PROGRAMS "${CMAKE_SOURCE_DIR}/tools/beam-application.py" DESTINATION share/beam/tools)
