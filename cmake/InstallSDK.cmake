if(TARGET erl_child_setup)
    install(TARGETS erl_child_setup RUNTIME DESTINATION libexec/beam)
endif()
install(TARGETS libbeam EXPORT BEAMSharedTargets
    RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib FRAMEWORK DESTINATION lib)
install(TARGETS libbeam_static beam_runtime_static EXPORT BEAMStaticTargets ARCHIVE DESTINATION lib)
install(FILES "${CMAKE_SOURCE_DIR}/cmake/RuntimeExports.list" DESTINATION lib/beam)
if(BEAM_BUILD_CLI)
    install(TARGETS beam EXPORT BEAMCLITargets RUNTIME DESTINATION bin)
    add_executable(beam_launch "${CMAKE_SOURCE_DIR}/src/beam_launcher.c")
    set_target_properties(beam_launch PROPERTIES OUTPUT_NAME beam-launch)
    if(MINGW)
        target_link_options(beam_launch PRIVATE -municode)
    endif()
    install(TARGETS beam_launch RUNTIME DESTINATION bin)
endif()
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/beam" DESTINATION include)
install(TARGETS beam_sdk_support EXPORT BEAMTargets)
install(FILES "${CMAKE_SOURCE_DIR}/src/beam_bytecode_verify.c"
    "${CMAKE_SOURCE_DIR}/src/beam_bytecode_internal.h" "${_sdk_inventories}" DESTINATION share/beam/sdk)
install(FILES "${_base_expectations}" "${CMAKE_SOURCE_DIR}/cmake/BEAMBytecode.cmake"
    DESTINATION lib/cmake/BEAM/support)
install(FILES ${_base_outputs} DESTINATION lib/beam/library/ebin)
install(FILES "${_base_manifest}" DESTINATION lib/beam/library)
install(FILES "${_beam_host_beam}" DESTINATION lib/beam/ebin)
install(TARGETS ${BEAM_COMPONENT_TARGETS} LIBRARY DESTINATION lib RUNTIME DESTINATION bin FRAMEWORK DESTINATION lib)
install(FILES ${BEAM_INSTALLED_BEAM_FILES} DESTINATION lib/beam/runtime/beam/ebin)
install(FILES "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/modules.txt" DESTINATION lib/beam/runtime/beam)
install(TARGETS ${BEAM_NATIVE_NIF_TARGETS}
    LIBRARY DESTINATION lib/beam/runtime/beam/priv
    RUNTIME DESTINATION lib/beam/runtime/beam/priv)

# Public native-extension inputs; symbols resolve in the running VM.
add_library(beam_nif_sdk INTERFACE)
set_target_properties(beam_nif_sdk PROPERTIES EXPORT_NAME NIF)
target_include_directories(beam_nif_sdk INTERFACE $<INSTALL_INTERFACE:include/beam/nif> $<INSTALL_INTERFACE:include>)
install(TARGETS beam_nif_sdk EXPORT BEAMTargets)
add_library(beam_driver_sdk INTERFACE)
set_target_properties(beam_driver_sdk PROPERTIES EXPORT_NAME Driver)
target_include_directories(beam_driver_sdk INTERFACE $<INSTALL_INTERFACE:include/beam/nif> $<INSTALL_INTERFACE:include>)
install(TARGETS beam_driver_sdk EXPORT BEAMTargets)
if(APPLE)
    foreach(_native_sdk beam_nif_sdk beam_driver_sdk)
        # Native modules resolve the selected host's public API at load time.
        # Executables and static libraries must retain ordinary link checking.
        target_link_options(${_native_sdk} INTERFACE
            "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:LINKER:-undefined,dynamic_lookup>"
            "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:LINKER:-undefined,dynamic_lookup>")
    endforeach()
endif()
install(FILES
    "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_nif.h"
    "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_driver.h"
    "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_drv_nif.h"
    "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_nif_api_funcs.h"
    "${CMAKE_BINARY_DIR}/erts/include/erl_int_sizes_config.h"
    DESTINATION include/beam/nif)
if(WIN32)
    install(FILES "${CMAKE_SOURCE_DIR}/src/erts/emulator/sys/win32/erl_win_dyn_driver.h"
        DESTINATION include/beam/nif)
endif()
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/sdk.toml" CONTENT
"format = 1
identity = \"${BEAM_RELEASE_VERSION}\"
build-id = \"${BEAM_COMPONENT_BUILD_ID}\"
os = \"${CMAKE_SYSTEM_NAME}\"
architecture = \"${CMAKE_SYSTEM_PROCESSOR}\"
triple = \"${ERTS_HOST_TRIPLE}\"
flavor = \"${ERTS_BUILD_FLAVOR}\"
" NEWLINE_STYLE UNIX @ONLY)
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

install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMDeployment.cmake" DESTINATION lib/cmake/BEAM/support)

install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMNif.cmake" DESTINATION lib/cmake/BEAM/support)
install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMTarget.cmake" DESTINATION lib/cmake/BEAM/support)

install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMiOS.cmake" DESTINATION lib/cmake/BEAM/support)

# VM, public native headers, generic process helper and core inspection bytes.
set(_static_exports RuntimeExports.list)
if(APPLE)
    set(_static_exports RuntimeExports.macho)
endif()
set(BEAM_RUNTIME_ARTIFACT_RECORDS
    "api.beam|embedding-header|include/beam/beam.h|api.runtime,api.boot"
    "api.runtime|embedding-header|include/beam/runtime.h|-"
    "api.boot|embedding-header|include/beam/boot.h|api.runtime"
    "api.bytecode|embedding-header|include/beam/sdk/bytecode.h|api.runtime"
    "api.inventories|embedding-header|include/beam/sdk/inventories.h|api.bytecode"
    "sdk.inventories|embedding-source|share/beam/sdk/inventories.c|api.inventories"
    "sdk.bytecode.private|embedding-source|share/beam/sdk/beam_bytecode_internal.h|api.bytecode"
    "api.verifier|embedding-source|share/beam/sdk/beam_bytecode_verify.c|api.bytecode,sdk.bytecode.private"
    "sdk.native|consumer-metadata|lib/cmake/BEAM/support/BEAMNif.cmake|-"
    "sdk.target.check|consumer-metadata|lib/cmake/BEAM/support/BEAMTarget.cmake|-"
    "sdk.application|consumer-metadata|lib/cmake/BEAM/support/BEAMiOS.cmake|-"
    "sdk.android|consumer-metadata|lib/cmake/BEAM/support/BEAMAndroid.cmake|-"
    "sdk.android.native|consumer-metadata|lib/cmake/BEAM/support/BEAMVerifyAndroidNative.cmake|-"
    "sdk.android.assembly|consumer-tool|share/beam/tools/beam-android.py|-"
    "sdk.target|target-metadata|lib/beam/target.toml|compiler.generation"
    "compiler.generation|compiler-provenance|lib/beam/compiler-generation.json|-"
    "runtime|runtime|${BEAM_RUNTIME_RELATIVE}|-"
    "runtime.static|runtime-static|${BEAM_STATIC_RELATIVE}|link.static,link.static.configuration,link.static.exports"
    "link.static|link-support|lib/cmake/BEAM/BEAMStaticTargets.cmake|-"
    "link.static.configuration|link-support|lib/cmake/BEAM/BEAMStaticTargets-%CONFIG%.cmake|-"
    "link.static.exports|link-support|lib/beam/${_static_exports}|-"
)
if(BEAM_APPLE_FRAMEWORKS)
    foreach(_pair IN ITEMS "beam|beam.h" "runtime|runtime.h" "boot|boot.h" "bytecode|sdk/bytecode.h" "inventories|sdk/inventories.h")
        string(REPLACE "|" ";" _fields "${_pair}")
        list(GET _fields 0 _id)
        list(GET _fields 1 _header)
        list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
            "framework.runtime.${_id}|framework-header|lib/BEAM.framework/Headers/beam/${_header}|api.${_id}")
    endforeach()
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "framework.runtime.native|framework-header|lib/BEAM.framework/Headers/beam/native.h|header.beam_native"
        "framework.runtime.modulemap|framework-metadata|lib/BEAM.framework/Modules/module.modulemap|api.beam"
        "framework.runtime.info|framework-metadata|lib/BEAM.framework/Info.plist|-")
    foreach(_component IN ITEMS file network zlib)
        list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
            "framework.${_component}.info|framework-metadata|lib/BEAM${_component}.framework/Info.plist|-")
    endforeach()
endif()
if(CMAKE_SYSTEM_NAME MATCHES "^(iOS|Android)$")
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "sdk.toolchain|consumer-toolchain|lib/cmake/BEAM/BEAMToolchain.cmake|sdk.target")
endif()
if(BEAM_BUILD_CLI)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "command.beam|executable-helper|bin/beam${CMAKE_EXECUTABLE_SUFFIX}|runtime"
        "command.launcher|executable-helper|bin/beam-launch${CMAKE_EXECUTABLE_SUFFIX}|-")
endif()
if(TARGET erl_child_setup)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS "helper.child_setup|executable-helper|libexec/beam/erl_child_setup|-")
endif()
list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
    "header.beam_native|native-header|include/beam/native.h|-"
    "header.erl_nif|native-header|include/beam/nif/erl_nif.h|header.erl_drv_nif,header.erl_nif_api_funcs"
    "header.erl_driver|native-header|include/beam/nif/erl_driver.h|header.erl_drv_nif"
    "header.erl_drv_nif|native-header|include/beam/nif/erl_drv_nif.h|header.erl_int_sizes_config"
    "header.erl_nif_api_funcs|native-header|include/beam/nif/erl_nif_api_funcs.h|-"
    "header.erl_int_sizes_config|native-header|include/beam/nif/erl_int_sizes_config.h|-")
if(WIN32)
    list(REMOVE_ITEM BEAM_RUNTIME_ARTIFACT_RECORDS
        "header.erl_driver|native-header|include/beam/nif/erl_driver.h|header.erl_drv_nif")
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "header.erl_driver|native-header|include/beam/nif/erl_driver.h|header.erl_drv_nif,header.erl_win_dyn_driver"
        "header.erl_win_dyn_driver|native-header|include/beam/nif/erl_win_dyn_driver.h|-")
endif()
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
install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMAndroid.cmake" DESTINATION lib/cmake/BEAM/support)
install(PROGRAMS "${CMAKE_SOURCE_DIR}/tools/beam-android.py" DESTINATION share/beam/tools)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.txt" DESTINATION share/licenses/BEAM)
install(FILES "${CMAKE_SOURCE_DIR}/AUTHORS" DESTINATION share/licenses/BEAM)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/LICENSES" DESTINATION share/licenses/BEAM)
list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
    "license.BEAM|license|share/licenses/BEAM/LICENSE.txt|-"
    "license.authors|license|share/licenses/BEAM/AUTHORS|-"
    "license.zlib|license|share/licenses/BEAM/zlib/LICENSE.txt|-")
file(GLOB_RECURSE _beam_licenses RELATIVE "${CMAKE_SOURCE_DIR}/LICENSES" "${CMAKE_SOURCE_DIR}/LICENSES/*")
foreach(_license IN LISTS _beam_licenses)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS "license.root.${_license}|license|share/licenses/BEAM/LICENSES/${_license}|-")
endforeach()
foreach(_license IN ITEMS "asmjit/LICENSE.md" "ryu/LICENSE-Apache2" "ryu/LICENSE-Boost" "ryu/xcharconv_ryu.h.sha.license" "pcre/LICENCE" "zstd/LICENSE" "zstd/COPYING")
    get_filename_component(_directory "${_license}" DIRECTORY)
    install(FILES "${CMAKE_SOURCE_DIR}/src/erts/emulator/${_license}" DESTINATION "share/licenses/BEAM/${_directory}")
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS "license.${_license}|license|share/licenses/BEAM/${_license}|-")
endforeach()

install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMVerifyAndroidNative.cmake" DESTINATION lib/cmake/BEAM/support)

file(READ "${CMAKE_SOURCE_DIR}/src/erts/emulator/zlib/zlib.h" _zlib_header)
string(FIND "${_zlib_header}" "*/" _zlib_notice_end)
math(EXPR _zlib_notice_end "${_zlib_notice_end} + 2")
string(SUBSTRING "${_zlib_header}" 0 ${_zlib_notice_end} _zlib_notice)
file(WRITE "${CMAKE_BINARY_DIR}/zlib-LICENSE.txt" "${_zlib_notice}\n")
install(FILES "${CMAKE_BINARY_DIR}/zlib-LICENSE.txt" DESTINATION share/licenses/BEAM/zlib RENAME LICENSE.txt)
if(MINGW)
    get_filename_component(BEAM_WINDOWS_RUNTIME_DIRECTORY "${CMAKE_C_COMPILER}" DIRECTORY)
    set(BEAM_WINDOWS_CXX_RUNTIME "")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        find_file(_beam_windows_cxx_runtime NAMES libstdc++-6.dll
            PATHS "${BEAM_WINDOWS_RUNTIME_DIRECTORY}" NO_DEFAULT_PATH REQUIRED)
        set(BEAM_WINDOWS_CXX_RUNTIME "${_beam_windows_cxx_runtime}")
    endif()
    set(BEAM_WINDOWS_PRODUCT_DLLS)
    foreach(_target IN ITEMS libbeam ${BEAM_COMPONENT_TARGETS} ${BEAM_NATIVE_NIF_TARGETS})
        list(APPEND BEAM_WINDOWS_PRODUCT_DLLS "$<TARGET_FILE_NAME:${_target}>")
    endforeach()
    configure_file("${CMAKE_SOURCE_DIR}/cmake/InstallWindowsRuntime.cmake.in"
        "${CMAKE_BINARY_DIR}/InstallWindowsRuntime.cmake" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/InstallWindowsRuntime-$<CONFIG>.cmake"
        INPUT "${CMAKE_BINARY_DIR}/InstallWindowsRuntime.cmake")
    install(SCRIPT "${CMAKE_BINARY_DIR}/InstallWindowsRuntime-$<CONFIG>.cmake")
    foreach(_license_package IN ITEMS gcc-libs winpthreads libwinpthread)
        set(_license_directory "${BEAM_WINDOWS_RUNTIME_DIRECTORY}/../share/licenses/${_license_package}")
        if(EXISTS "${_license_directory}")
            install(DIRECTORY "${_license_directory}/" DESTINATION "share/licenses/BEAM/${_license_package}")
            file(GLOB_RECURSE _licenses LIST_DIRECTORIES FALSE RELATIVE "${_license_directory}" "${_license_directory}/*")
            foreach(_license IN LISTS _licenses)
                list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
                    "license.windows.${_license_package}.${_license}|license|share/licenses/BEAM/${_license_package}/${_license}|-")
            endforeach()
        endif()
    endforeach()
endif()
# Portable desktop discovery is limited to shared runtime linkage.
if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|Windows)$")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/beam.pc.in" "${CMAKE_BINARY_DIR}/beam.pc" @ONLY)
    install(FILES "${CMAKE_BINARY_DIR}/beam.pc" DESTINATION lib/pkgconfig)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "sdk.pkgconfig|consumer-metadata|lib/pkgconfig/beam.pc|-")
endif()

configure_file("${CMAKE_SOURCE_DIR}/cmake/InstallRuntimeArtifacts.cmake.in"
    "${CMAKE_BINARY_DIR}/InstallRuntimeArtifacts.cmake" @ONLY)
install(SCRIPT "${CMAKE_BINARY_DIR}/InstallRuntimeArtifacts.cmake")
install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMRuntimeArtifacts.cmake" DESTINATION lib/cmake/BEAM/support)

if(MSVC AND BEAM_RELEASE_DEBUG_INFO)
    foreach(_debug_target IN ITEMS libbeam beam ${BEAM_COMPONENT_TARGETS} ${BEAM_NATIVE_NIF_TARGETS})
        if(TARGET ${_debug_target})
            install(FILES "$<TARGET_PDB_FILE:${_debug_target}>" DESTINATION bin OPTIONAL)
        endif()
    endforeach()
endif()

install(PROGRAMS "${CMAKE_SOURCE_DIR}/tools/beam-application.py" DESTINATION share/beam/tools)

install(PROGRAMS "${CMAKE_SOURCE_DIR}/tools/beam-xcframework.py" DESTINATION share/beam/tools)

include("${CMAKE_SOURCE_DIR}/cmake/InstallDocumentation.cmake")
