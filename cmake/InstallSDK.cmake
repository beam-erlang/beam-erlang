# SDK exports and installed artifacts. File declarations own placement and catalog
# metadata; target installation and exports remain ordinary CMake declarations.
set(BEAM_RUNTIME_ARTIFACT_RECORDS)
function(_beam_install_sdk_file id kind source relative)
    cmake_parse_arguments(PARSE_ARGV 4 FILE "PROGRAM" "" "REQUIRES")
    if(FILE_UNPARSED_ARGUMENTS OR FILE_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Invalid SDK file declaration: ${id}")
    endif()
    get_filename_component(_directory "${relative}" DIRECTORY)
    get_filename_component(_name "${relative}" NAME)
    if(FILE_PROGRAM)
        install(PROGRAMS "${source}" DESTINATION "${_directory}" RENAME "${_name}")
    else()
        install(FILES "${source}" DESTINATION "${_directory}" RENAME "${_name}")
    endif()
    set(_requires "-")
    if(FILE_REQUIRES)
        list(JOIN FILE_REQUIRES "," _requires)
    endif()
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS "${id}|${kind}|${relative}|${_requires}")
    set(BEAM_RUNTIME_ARTIFACT_RECORDS "${BEAM_RUNTIME_ARTIFACT_RECORDS}" PARENT_SCOPE)
endfunction()

# Public header and license trees use 0755; their implicit parent directories
# retain the installation defaults.
function(_beam_install_sdk_directories root)
    set(_directories "${root}")
    foreach(_file IN LISTS ARGN)
        get_filename_component(_directory "${_file}" DIRECTORY)
        while(NOT _directory STREQUAL "")
            list(APPEND _directories "${root}/${_directory}")
            get_filename_component(_directory "${_directory}" DIRECTORY)
        endwhile()
    endforeach()
    list(REMOVE_DUPLICATES _directories)
    list(SORT _directories)
    foreach(_directory IN LISTS _directories)
        install(DIRECTORY DESTINATION "${_directory}" DIRECTORY_PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endforeach()
endfunction()

set(_component_targets beam_file_component beam_network_component beam_zlib_component)
set(_native_targets beam_erl_tracer_nif)
if(TARGET erl_child_setup)
    install(TARGETS erl_child_setup RUNTIME DESTINATION libexec/beam)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "helper.child_setup|executable-helper|libexec/beam/erl_child_setup|-")
endif()
install(TARGETS libbeam EXPORT BEAMSharedTargets
    RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib FRAMEWORK DESTINATION lib)
install(TARGETS libbeam_static beam_runtime_static EXPORT BEAMStaticTargets ARCHIVE DESTINATION lib)
install(TARGETS beam_host_support EXPORT BEAMHostTargets ARCHIVE DESTINATION lib)
install(EXPORT BEAMHostTargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
install(FILES "${CMAKE_SOURCE_DIR}/cmake/RuntimeExports.list" DESTINATION lib/beam)
if(BEAM_BUILD_CLI)
    install(TARGETS beam EXPORT BEAMCLITargets RUNTIME DESTINATION bin)
    install(TARGETS beam_launch RUNTIME DESTINATION bin)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "command.beam|executable-helper|bin/beam${CMAKE_EXECUTABLE_SUFFIX}|runtime"
        "command.launcher|executable-helper|bin/beam-launch${CMAKE_EXECUTABLE_SUFFIX}|-")
endif()
install(TARGETS beam_sdk_support EXPORT BEAMTargets)
get_target_property(_inventory_source beam_sdk_support BEAM_SDK_INVENTORY_SOURCE)
_beam_install_sdk_file(sdk.inventories embedding-source "${_inventory_source}"
    share/beam/sdk/inventories.c REQUIRES api.inventories)
_beam_install_sdk_file(sdk.bytecode.private embedding-source "${CMAKE_SOURCE_DIR}/src/beam_bytecode_internal.h"
    share/beam/sdk/beam_bytecode_internal.h REQUIRES api.bytecode)
_beam_install_sdk_file(api.verifier embedding-source "${CMAKE_SOURCE_DIR}/src/beam_bytecode_verify.c"
    share/beam/sdk/beam_bytecode_verify.c REQUIRES api.bytecode sdk.bytecode.private)
foreach(_inventory IN ITEMS base boot services startup host output code)
    foreach(_field IN ITEMS MODULES FILES MANIFEST EXPECTATIONS INSTALL_ROOT)
        get_target_property(_${_field} beam_${_inventory}_inventory BEAM_INVENTORY_${_field})
        if(NOT _${_field})
            message(FATAL_ERROR "Incomplete SDK ${_inventory} inventory: ${_field}")
        endif()
    endforeach()
    string(TOUPPER "${_inventory}" _upper)
    set(BEAM_${_upper}_MODULES "${_MODULES}")
    set(BEAM_${_upper}_INSTALL_ROOT "${_INSTALL_ROOT}")
    install(FILES ${_FILES} DESTINATION "${_INSTALL_ROOT}/ebin")
    install(FILES "${_MANIFEST}" DESTINATION "${_INSTALL_ROOT}")
    install(FILES "${_EXPECTATIONS}" DESTINATION lib/cmake/BEAM/support)
endforeach()
get_target_property(_host_artifact beam_host_artifact BEAM_HOST_ARTIFACT)
install(FILES "${_host_artifact}" DESTINATION lib/beam/ebin)
get_target_property(_service_files beam_service_artifacts BEAM_INVENTORY_FILES)
get_target_property(_service_manifest beam_service_artifacts BEAM_INVENTORY_MANIFEST)
get_target_property(_service_root beam_service_artifacts BEAM_INVENTORY_INSTALL_ROOT)
install(FILES ${_service_files} DESTINATION "${_service_root}/ebin")
install(FILES "${_service_manifest}" DESTINATION "${_service_root}")
foreach(_library file network zlib)
    install(TARGETS beam_${_library}_component EXPORT BEAM${_library}SharedTargets
        LIBRARY DESTINATION lib RUNTIME DESTINATION bin FRAMEWORK DESTINATION lib)
    install(TARGETS beam_${_library}_component_static EXPORT BEAM${_library}StaticTargets ARCHIVE DESTINATION lib)
    install(EXPORT BEAM${_library}SharedTargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
    install(EXPORT BEAM${_library}StaticTargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
endforeach()
install(TARGETS ${_native_targets}
    LIBRARY DESTINATION lib/beam/runtime/beam/priv RUNTIME DESTINATION lib/beam/runtime/beam/priv)

# Public native-extension inputs; symbols resolve in the running VM.
foreach(_kind IN ITEMS nif driver)
    add_library(beam_${_kind}_sdk INTERFACE)
    set(_export NIF)
    if(_kind STREQUAL driver)
        set(_export Driver)
    endif()
    set_target_properties(beam_${_kind}_sdk PROPERTIES EXPORT_NAME "${_export}")
    target_include_directories(beam_${_kind}_sdk INTERFACE
        $<INSTALL_INTERFACE:include/beam/nif> $<INSTALL_INTERFACE:include>)
    install(TARGETS beam_${_kind}_sdk EXPORT BEAMTargets)
    if(APPLE)
        # Executables/static libraries retain normal link checking.
        target_link_options(beam_${_kind}_sdk INTERFACE
            "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:LINKER:-undefined,dynamic_lookup>"
            "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:LINKER:-undefined,dynamic_lookup>")
    endif()
endforeach()
_beam_install_sdk_directories(include/beam ${BEAM_PUBLIC_HEADERS})
foreach(_header IN LISTS BEAM_PUBLIC_HEADERS)
    get_filename_component(_id "${_header}" NAME_WE)
    set(_kind embedding-header)
    set(_requires "-")
    if(_id STREQUAL beam)
        set(_requires api.runtime)
    elseif(_id STREQUAL host)
        set(_requires api.library api.native_image)
    elseif(_id STREQUAL boot)
        set(_requires api.host)
    elseif(_id STREQUAL bytecode OR _id STREQUAL library OR _id STREQUAL native_image)
        set(_requires api.runtime)
    elseif(_id STREQUAL inventories)
        set(_requires api.bytecode)
    endif()
    set(_id "api.${_id}")
    if(_header STREQUAL native.h)
        set(_id header.beam_native)
        set(_kind native-header)
    endif()
    _beam_install_sdk_file("${_id}" "${_kind}" "${CMAKE_SOURCE_DIR}/include/beam/${_header}"
        "include/beam/${_header}" REQUIRES ${_requires})
    if(BEAM_APPLE_FRAMEWORKS)
        get_filename_component(_framework_id "${_header}" NAME_WE)
        list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
            "framework.runtime.${_framework_id}|framework-header|lib/BEAM.framework/Headers/beam/${_header}|${_id}")
    endif()
endforeach()
foreach(_header IN ITEMS erl_nif erl_driver erl_drv_nif erl_nif_api_funcs erl_int_sizes_config erl_win_dyn_driver)
    set(_source "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/${_header}.h")
    set(_requires "-")
    if(_header STREQUAL erl_nif)
        set(_requires header.erl_drv_nif header.erl_nif_api_funcs)
    elseif(_header STREQUAL erl_driver)
        set(_requires header.erl_drv_nif)
        if(WIN32)
            list(APPEND _requires header.erl_win_dyn_driver)
        endif()
    elseif(_header STREQUAL erl_drv_nif)
        set(_requires header.erl_int_sizes_config)
    elseif(_header STREQUAL erl_int_sizes_config)
        set(_source "${CMAKE_BINARY_DIR}/erts/include/${_header}.h")
    elseif(_header STREQUAL erl_win_dyn_driver)
        if(NOT WIN32)
            continue()
        endif()
        set(_source "${CMAKE_SOURCE_DIR}/src/erts/emulator/sys/win32/${_header}.h")
    endif()
    _beam_install_sdk_file("header.${_header}" native-header "${_source}"
        "include/beam/nif/${_header}.h" REQUIRES ${_requires})
endforeach()
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
include("${CMAKE_CURRENT_LIST_DIR}/InstallTargetMetadata.cmake")

set(BEAM_RUNTIME_RELATIVE "lib/${CMAKE_SHARED_LIBRARY_PREFIX}beam${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(BEAM_STATIC_RELATIVE "lib/${CMAKE_STATIC_LIBRARY_PREFIX}beam${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_component_install_dir lib)
if(WIN32)
    set(BEAM_RUNTIME_RELATIVE "bin/${CMAKE_SHARED_LIBRARY_PREFIX}beam${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(BEAM_STATIC_RELATIVE "lib/${CMAKE_STATIC_LIBRARY_PREFIX}beam_static${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(_component_install_dir bin)
elseif(BEAM_APPLE_FRAMEWORKS)
    set(BEAM_RUNTIME_RELATIVE lib/BEAM.framework/BEAM)
endif()
set(_runtime_requires "-")
if(ANDROID)
    find_file(_android_cxx_runtime libc++_shared.so
        PATHS "${CMAKE_SYSROOT}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
              "${CMAKE_SYSROOT}/usr/lib/${ANDROID_TOOLCHAIN_NAME}"
        NO_DEFAULT_PATH REQUIRED)
    _beam_install_sdk_file(runtime.cxx runtime-dependency "${_android_cxx_runtime}" lib/libc++_shared.so)
    set(_runtime_requires runtime.cxx)
endif()
set(_static_exports RuntimeExports.list)
if(APPLE)
    set(_static_exports RuntimeExports.macho)
endif()
list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
    "runtime|runtime|${BEAM_RUNTIME_RELATIVE}|${_runtime_requires}"
    "runtime.static|runtime-static|${BEAM_STATIC_RELATIVE}|link.static,link.static.configuration,link.static.exports"
    "host.static|host-static|lib/${CMAKE_STATIC_LIBRARY_PREFIX}beam-host${CMAKE_STATIC_LIBRARY_SUFFIX}|api.boot,api.host,api.library,api.native_image,link.host,link.host.configuration"
    "link.host|link-support|lib/cmake/BEAM/BEAMHostTargets.cmake|-"
    "link.host.configuration|link-support|lib/cmake/BEAM/BEAMHostTargets-%CONFIG%.cmake|-"
    "link.static|link-support|lib/cmake/BEAM/BEAMStaticTargets.cmake|-"
    "link.static.configuration|link-support|lib/cmake/BEAM/BEAMStaticTargets-%CONFIG%.cmake|-"
    "link.static.exports|link-support|lib/beam/${_static_exports}|-")
foreach(_component IN ITEMS file network zlib)
    set(_relative "${_component_install_dir}/${CMAKE_SHARED_LIBRARY_PREFIX}beam-${_component}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(BEAM_APPLE_FRAMEWORKS)
        set(_relative "lib/BEAM${_component}.framework/BEAM${_component}")
        list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
            "framework.${_component}.info|framework-metadata|lib/BEAM${_component}.framework/Info.plist|-")
    endif()
    string(TOUPPER "${_component}" _upper)
    set(BEAM_${_upper}_RELATIVE "${_relative}")
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "library.${_component}.shared|native-library|${_relative}|runtime"
        "library.${_component}.static|native-library-static|lib/${CMAKE_STATIC_LIBRARY_PREFIX}beam-${_component}${CMAKE_STATIC_LIBRARY_SUFFIX}|library.${_component}.header,link.library.${_component}.static,link.library.${_component}.static.configuration"
        "library.${_component}.header|embedding-header|include/beam/libraries/${_component}.h|api.library"
        "link.library.${_component}.shared|link-support|lib/cmake/BEAM/BEAM${_component}SharedTargets.cmake|-"
        "link.library.${_component}.shared.configuration|link-support|lib/cmake/BEAM/BEAM${_component}SharedTargets-%CONFIG%.cmake|-"
        "link.library.${_component}.static|link-support|lib/cmake/BEAM/BEAM${_component}StaticTargets.cmake|-"
        "link.library.${_component}.static.configuration|link-support|lib/cmake/BEAM/BEAM${_component}StaticTargets-%CONFIG%.cmake|-")
endforeach()
if(BEAM_APPLE_FRAMEWORKS)
    list(APPEND BEAM_RUNTIME_ARTIFACT_RECORDS
        "framework.runtime.modulemap|framework-metadata|lib/BEAM.framework/Modules/module.modulemap|api.beam"
        "framework.runtime.info|framework-metadata|lib/BEAM.framework/Info.plist|-")
endif()
# These are the exact inputs embedded in libbeam, exposed for inspection only.
foreach(_module IN LISTS BEAM_CORE_MODULES)
    _beam_install_sdk_file("core.${_module}" core-module
        "${CMAKE_SOURCE_DIR}/src/erts/preloaded/ebin/${_module}.beam" "lib/beam/core/ebin/${_module}.beam")
    _beam_install_sdk_file("core-source.${_module}" core-source
        "${CMAKE_SOURCE_DIR}/src/erts/preloaded/src/${_module}.erl" "lib/beam/core/src/${_module}.erl")
endforeach()
foreach(_record IN ITEMS
        "native|BEAMNif.cmake" "library|BEAMLibrary.cmake" "target.check|BEAMTarget.cmake" "application|BEAMiOS.cmake"
        "android|BEAMAndroid.cmake" "android.native|BEAMVerifyAndroidNative.cmake")
    string(REPLACE "|" ";" _fields "${_record}")
    list(GET _fields 0 _id)
    list(GET _fields 1 _file)
    _beam_install_sdk_file("sdk.${_id}" consumer-metadata "${CMAKE_CURRENT_LIST_DIR}/sdk/${_file}"
        "lib/cmake/BEAM/support/${_file}")
endforeach()
install(FILES "${CMAKE_CURRENT_LIST_DIR}/sdk/BEAMBytecode.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/sdk/BEAMDeployment.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/sdk/BEAMRuntimeArtifacts.cmake" DESTINATION lib/cmake/BEAM/support)
_beam_install_sdk_file(sdk.android.assembly consumer-tool "${CMAKE_SOURCE_DIR}/tools/beam-android.py"
    share/beam/tools/beam-android.py PROGRAM)
_beam_install_sdk_file(sdk.library.author consumer-tool "${CMAKE_SOURCE_DIR}/tools/beam-library.py"
    share/beam/tools/beam-library.py PROGRAM)

include(CMakePackageConfigHelpers)
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/sdk/BEAMConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/BEAMConfig.cmake" INSTALL_DESTINATION lib/cmake/BEAM)
# The runtime release number is not a promise of C API compatibility.
write_basic_package_version_file("${CMAKE_BINARY_DIR}/BEAMConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(EXPORT BEAMTargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
install(EXPORT BEAMSharedTargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
install(EXPORT BEAMStaticTargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
if(BEAM_BUILD_CLI)
    install(EXPORT BEAMCLITargets NAMESPACE BEAM:: DESTINATION lib/cmake/BEAM)
endif()
install(FILES "${CMAKE_BINARY_DIR}/BEAMConfig.cmake" "${CMAKE_BINARY_DIR}/BEAMConfigVersion.cmake"
    DESTINATION lib/cmake/BEAM)

_beam_install_sdk_file(license.BEAM license "${CMAKE_SOURCE_DIR}/LICENSE.txt" share/licenses/BEAM/LICENSE.txt)
_beam_install_sdk_file(license.authors license "${CMAKE_SOURCE_DIR}/AUTHORS" share/licenses/BEAM/AUTHORS)
file(GLOB_RECURSE _licenses LIST_DIRECTORIES FALSE RELATIVE "${CMAKE_SOURCE_DIR}/LICENSES"
    "${CMAKE_SOURCE_DIR}/LICENSES/*")
_beam_install_sdk_directories(share/licenses/BEAM/LICENSES ${_licenses})
foreach(_license IN LISTS _licenses)
    _beam_install_sdk_file("license.root.${_license}" license "${CMAKE_SOURCE_DIR}/LICENSES/${_license}"
        "share/licenses/BEAM/LICENSES/${_license}")
endforeach()
foreach(_license IN ITEMS "asmjit/LICENSE.md" "ryu/LICENSE-Apache2" "ryu/LICENSE-Boost"
        "ryu/xcharconv_ryu.h.sha.license" "pcre/LICENCE")
    _beam_install_sdk_file("license.${_license}" license "${CMAKE_SOURCE_DIR}/src/erts/emulator/${_license}"
        "share/licenses/BEAM/${_license}")
endforeach()
file(READ "${CMAKE_SOURCE_DIR}/src/erts/emulator/zlib/zlib.h" _zlib_header)
string(FIND "${_zlib_header}" "*/" _zlib_notice_end)
math(EXPR _zlib_notice_end "${_zlib_notice_end} + 2")
string(SUBSTRING "${_zlib_header}" 0 ${_zlib_notice_end} _zlib_notice)
file(WRITE "${CMAKE_BINARY_DIR}/zlib-LICENSE.txt" "${_zlib_notice}\n")
_beam_install_sdk_file(license.zlib license "${CMAKE_BINARY_DIR}/zlib-LICENSE.txt" share/licenses/BEAM/zlib/LICENSE.txt)
if(MINGW)
    get_filename_component(BEAM_WINDOWS_RUNTIME_DIRECTORY "${CMAKE_C_COMPILER}" DIRECTORY)
    set(BEAM_WINDOWS_CXX_RUNTIME "")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        find_file(_beam_windows_cxx_runtime NAMES libstdc++-6.dll
            PATHS "${BEAM_WINDOWS_RUNTIME_DIRECTORY}" NO_DEFAULT_PATH REQUIRED)
        set(BEAM_WINDOWS_CXX_RUNTIME "${_beam_windows_cxx_runtime}")
    endif()
    set(BEAM_WINDOWS_PRODUCT_DLLS)
    foreach(_target IN ITEMS libbeam ${_component_targets} ${_native_targets})
        list(APPEND BEAM_WINDOWS_PRODUCT_DLLS "$<TARGET_FILE_NAME:${_target}>")
    endforeach()
    configure_file("${CMAKE_CURRENT_LIST_DIR}/InstallWindowsRuntime.cmake.in"
        "${CMAKE_BINARY_DIR}/InstallWindowsRuntime.cmake" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/InstallWindowsRuntime-$<CONFIG>.cmake"
        INPUT "${CMAKE_BINARY_DIR}/InstallWindowsRuntime.cmake")
    install(SCRIPT "${CMAKE_BINARY_DIR}/InstallWindowsRuntime-$<CONFIG>.cmake")
    foreach(_package IN ITEMS gcc-libs winpthreads libwinpthread)
        set(_directory "${BEAM_WINDOWS_RUNTIME_DIRECTORY}/../share/licenses/${_package}")
        if(EXISTS "${_directory}")
            file(GLOB_RECURSE _licenses LIST_DIRECTORIES FALSE RELATIVE "${_directory}" "${_directory}/*")
            foreach(_license IN LISTS _licenses)
                _beam_install_sdk_file("license.windows.${_package}.${_license}" license "${_directory}/${_license}"
                    "share/licenses/BEAM/${_package}/${_license}")
            endforeach()
        endif()
    endforeach()
endif()
if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|Windows)$")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/sdk/beam.pc.in" "${CMAKE_BINARY_DIR}/beam.pc" @ONLY)
    _beam_install_sdk_file(sdk.pkgconfig consumer-metadata "${CMAKE_BINARY_DIR}/beam.pc" lib/pkgconfig/beam.pc)
endif()
# Hash final installed bytes after platform dependency discovery and RPATH edits.
configure_file("${CMAKE_CURRENT_LIST_DIR}/InstallRuntimeArtifacts.cmake.in"
    "${CMAKE_BINARY_DIR}/InstallRuntimeArtifacts.cmake" @ONLY)
install(SCRIPT "${CMAKE_BINARY_DIR}/InstallRuntimeArtifacts.cmake")
if(MSVC AND BEAM_RELEASE_DEBUG_INFO)
    foreach(_target IN ITEMS libbeam beam ${_component_targets} ${_native_targets})
        if(TARGET ${_target})
            install(FILES "$<TARGET_PDB_FILE:${_target}>" DESTINATION bin OPTIONAL)
        endif()
    endforeach()
endif()
install(PROGRAMS "${CMAKE_SOURCE_DIR}/tools/beam-application.py"
    "${CMAKE_SOURCE_DIR}/tools/beam-xcframework.py" DESTINATION share/beam/tools)
include("${CMAKE_CURRENT_LIST_DIR}/InstallDocumentation.cmake")
