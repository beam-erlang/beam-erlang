include(CMakePackageConfigHelpers)
include("${CMAKE_CURRENT_LIST_DIR}/SDKManifest.cmake")
# SDK inventories and assembly remain relocatable only with prefix-relative
# install directories; reject unsupported layouts during configuration.
foreach(_beam_install_dir CMAKE_INSTALL_BINDIR CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_DATADIR)
    if(IS_ABSOLUTE "${${_beam_install_dir}}" OR "${${_beam_install_dir}}" MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "BEAM SDK requires relative install directories: ${_beam_install_dir}=${${_beam_install_dir}}")
    endif()
endforeach()

set(BEAM_SDK_VERSION "${BEAM_VERSION}")
set(BEAM_SDK_ERTS_VERSION "${BEAM_VERSION}")
file(STRINGS "${CMAKE_SOURCE_DIR}/UPSTREAM_COMMIT" BEAM_SDK_UPSTREAM_COMMIT LIMIT_COUNT 1)
set(BEAM_SDK_FLAVOR "${ERTS_BUILD_FLAVOR}")
set(BEAM_SDK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
set(BEAM_SDK_ARCHITECTURE "${ERTS_TARGET_ARCH}")
set(BEAM_SDK_HOST_TRIPLE "${ERTS_HOST_TRIPLE}")
set(BEAM_SDK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}")
set(BEAM_SDK_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
set(BEAM_SDK_DYNAMIC_TRACE "none")
set(BEAM_SDK_MSVC_RUNTIME_LIBRARY "")
if(MSVC)
    set(_beam_debug_runtime_suffix "")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_beam_debug_runtime_suffix "Debug")
    endif()
    if(CMAKE_MSVC_RUNTIME_LIBRARY)
        set(BEAM_SDK_MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
        string(REPLACE "$<$<CONFIG:Debug>:Debug>" "${_beam_debug_runtime_suffix}"
            BEAM_SDK_MSVC_RUNTIME_LIBRARY "${BEAM_SDK_MSVC_RUNTIME_LIBRARY}")
        if(NOT BEAM_SDK_MSVC_RUNTIME_LIBRARY MATCHES "^MultiThreaded(Debug)?(DLL)?$")
            message(FATAL_ERROR "BEAM SDK requires a concrete MSVC runtime or the standard per-config Debug expression")
        endif()
    else()
        set(BEAM_SDK_MSVC_RUNTIME_LIBRARY "MultiThreaded${_beam_debug_runtime_suffix}DLL")
    endif()
endif()

# This is an exact production input/configuration identity, not an ABI promise
# or an archive checksum. Test sources/options and output locations do not enter
# it. Hash the retained bootstrap because it produces SDK inspection metadata.
file(GLOB_RECURSE _beam_sdk_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    RELATIVE "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/erts/*" "${CMAKE_SOURCE_DIR}/lib/*"
    "${CMAKE_SOURCE_DIR}/include/*" "${CMAKE_SOURCE_DIR}/cmake/*"
    "${CMAKE_SOURCE_DIR}/bootstrap/*")
list(FILTER _beam_sdk_inputs EXCLUDE REGEX "(^|/)(test|tests)(/|$)")
list(APPEND _beam_sdk_inputs CMakeLists.txt BEAM_VERSION UPSTREAM_COMMIT)
list(SORT _beam_sdk_inputs)
set(_beam_sdk_identity "BEAM-SDK-inputs-v1")
foreach(_beam_input IN LISTS _beam_sdk_inputs)
    if(IS_SYMLINK "${CMAKE_SOURCE_DIR}/${_beam_input}")
        file(READ_SYMLINK "${CMAKE_SOURCE_DIR}/${_beam_input}" _beam_link_target)
        string(SHA256 _beam_hash "symlink:${_beam_link_target}")
    elseif(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/${_beam_input}")
        continue()
    else()
        file(SHA256 "${CMAKE_SOURCE_DIR}/${_beam_input}" _beam_hash)
    endif()
    string(APPEND _beam_sdk_identity "\n${_beam_input}:${_beam_hash}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_beam_sdk_inputs})
string(SHA256 BEAM_SDK_SOURCE_ID "${_beam_sdk_identity}")

string(TOUPPER "${CMAKE_BUILD_TYPE}" _beam_configuration)
foreach(_beam_key BEAM_SDK_VERSION BEAM_SDK_UPSTREAM_COMMIT BEAM_SDK_ERTS_VERSION
        BEAM_SDK_FLAVOR BEAM_SDK_BUILD_TYPE BEAM_SDK_ARCHITECTURE BEAM_SDK_HOST_TRIPLE
        BEAM_SDK_SYSTEM_NAME BEAM_SDK_DEPLOYMENT_TARGET BEAM_SDK_DYNAMIC_TRACE
        BEAM_SDK_MSVC_RUNTIME_LIBRARY
        CMAKE_SYSTEM_VERSION CMAKE_C_COMPILER_ID CMAKE_C_COMPILER_VERSION
        CMAKE_C_COMPILER_TARGET CMAKE_CXX_COMPILER_ID CMAKE_CXX_COMPILER_VERSION
        CMAKE_CXX_COMPILER_TARGET CMAKE_ASM_COMPILER_ID CMAKE_ASM_COMPILER_VERSION
        CMAKE_RC_COMPILER_ID CMAKE_RC_COMPILER_VERSION MSVC_VERSION
        CMAKE_MSVC_DEBUG_INFORMATION_FORMAT CMAKE_MSVC_RUNTIME_LIBRARY
        CMAKE_C_FLAGS CMAKE_C_FLAGS_${_beam_configuration}
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_${_beam_configuration}
        CMAKE_ASM_FLAGS CMAKE_ASM_FLAGS_${_beam_configuration}
        CMAKE_RC_FLAGS CMAKE_RC_FLAGS_${_beam_configuration}
        CMAKE_EXE_LINKER_FLAGS CMAKE_EXE_LINKER_FLAGS_${_beam_configuration}
        CMAKE_STATIC_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS_${_beam_configuration}
        CMAKE_POSITION_INDEPENDENT_CODE CMAKE_OSX_ARCHITECTURES ERTS_TERMCAP_LIB)
    set(_beam_value "${${_beam_key}}")
    # Explicit producer paths in flags must not make identical relocated source
    # and build trees acquire different identities.
    string(REPLACE "${CMAKE_BINARY_DIR}" "<BUILD>" _beam_value "${_beam_value}")
    string(REPLACE "${CMAKE_SOURCE_DIR}" "<SOURCE>" _beam_value "${_beam_value}")
    string(APPEND _beam_sdk_identity "\n${_beam_key}=${_beam_value}")
endforeach()
foreach(_beam_tool CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_ASM_COMPILER CMAKE_RC_COMPILER
        CMAKE_AR CMAKE_RANLIB CMAKE_LINKER PERL_EXECUTABLE)
    if(EXISTS "${${_beam_tool}}")
        file(SHA256 "${${_beam_tool}}" _beam_hash)
        string(APPEND _beam_sdk_identity "\n${_beam_tool}:${_beam_hash}")
    endif()
endforeach()
get_filename_component(_beam_sysroot_name "${CMAKE_OSX_SYSROOT}" NAME)
string(APPEND _beam_sdk_identity "\nsysroot=${_beam_sysroot_name}")
if(EXISTS "${CMAKE_OSX_SYSROOT}/SDKSettings.json")
    file(SHA256 "${CMAKE_OSX_SYSROOT}/SDKSettings.json" _beam_hash)
    string(APPEND _beam_sdk_identity "\nsysroot-settings:${_beam_hash}")
endif()
foreach(_beam_header config.h include/erl_int_sizes_config.h include/internal/ethread_header_config.h)
    file(SHA256 "${CMAKE_BINARY_DIR}/erts/${_beam_header}" _beam_hash)
    string(APPEND _beam_sdk_identity "\nconfigured/${_beam_header}:${_beam_hash}")
endforeach()
string(SHA256 BEAM_SDK_BUILD_ID "${_beam_sdk_identity}")

set(BEAM_SDK_NATIVE_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}/beam/erts")
set(BEAM_SDK_PRELOAD_EBIN_DIR "${CMAKE_INSTALL_DATADIR}/beam/erts-${BEAM_SDK_ERTS_VERSION}/ebin")
set(BEAM_SDK_PRELOAD_SOURCE_DIR "${CMAKE_INSTALL_DATADIR}/beam/erts-${BEAM_SDK_ERTS_VERSION}/src")
foreach(_beam_native_kind NIF Driver)
    add_library(beam_${_beam_native_kind} INTERFACE)
    add_library(BEAM::${_beam_native_kind} ALIAS beam_${_beam_native_kind})
    set_target_properties(beam_${_beam_native_kind} PROPERTIES EXPORT_NAME ${_beam_native_kind})
    target_include_directories(beam_${_beam_native_kind} INTERFACE
        $<INSTALL_INTERFACE:${BEAM_SDK_NATIVE_INCLUDE_DIR}>)
    if(APPLE)
        target_link_options(beam_${_beam_native_kind} INTERFACE "LINKER:-undefined,dynamic_lookup")
    endif()
endforeach()

set(_beam_sdk_native_headers
    "${CMAKE_SOURCE_DIR}/erts/emulator/beam/erl_nif.h"
    "${CMAKE_SOURCE_DIR}/erts/emulator/beam/erl_nif_api_funcs.h"
    "${CMAKE_SOURCE_DIR}/erts/emulator/beam/erl_driver.h"
    "${CMAKE_SOURCE_DIR}/erts/emulator/beam/erl_drv_nif.h"
    "${CMAKE_SOURCE_DIR}/erts/include/erl_fixed_size_int_types.h"
    "${CMAKE_BINARY_DIR}/erts/include/erl_int_sizes_config.h")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND _beam_sdk_native_headers
        "${CMAKE_SOURCE_DIR}/erts/emulator/sys/win32/erl_win_dyn_driver.h")
else()
    list(APPEND _beam_sdk_native_headers
        "${CMAKE_SOURCE_DIR}/erts/emulator/sys/unix/driver_int.h")
endif()
install(FILES "${CMAKE_SOURCE_DIR}/include/beam/beam.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/beam")
install(FILES ${_beam_sdk_native_headers} DESTINATION "${BEAM_SDK_NATIVE_INCLUDE_DIR}")
include("${CMAKE_SOURCE_DIR}/erts/preloaded/cmake/PreloadInspection.cmake")
install(DIRECTORY "${BEAM_PRELOAD_INSPECTION_DIR}/" DESTINATION "${BEAM_SDK_PRELOAD_EBIN_DIR}"
    FILES_MATCHING PATTERN "*.beam")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/erts/preloaded/src/"
    DESTINATION "${BEAM_SDK_PRELOAD_SOURCE_DIR}" USE_SOURCE_PERMISSIONS)
file(READ "${CMAKE_SOURCE_DIR}/erts/preloaded/src/erts.app.src" _beam_erts_app)
string(REPLACE "%VSN%" "${BEAM_SDK_ERTS_VERSION}" _beam_erts_app "${_beam_erts_app}")
string(REPLACE "%ESOCK_MODS%" "socket_registry,\n                prim_socket,\n                prim_net,"
    _beam_erts_app "${_beam_erts_app}")
file(WRITE "${CMAKE_BINARY_DIR}/sdk/erts.app" "${_beam_erts_app}")
install(FILES "${CMAKE_BINARY_DIR}/sdk/erts.app" DESTINATION "${BEAM_SDK_PRELOAD_EBIN_DIR}")

install(TARGETS beam_runtime_archive ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# Windows loads the canonical execution images from resources in the final
# executable. Publish source resources separately from the augmented inspection
# bytecode. Relative names resolve through the SDK resource include directory.
set(BEAM_SDK_PRELOAD_RESOURCE_DIR "${CMAKE_INSTALL_DATADIR}/beam/erts-${BEAM_SDK_ERTS_VERSION}/resources")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_beam_resource_code "")
    set(_beam_resource_dict "")
    set(_beam_resource_count 0)
    file(GLOB _beam_resource_seeds CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/*.beam")
    foreach(_beam_seed IN LISTS _beam_resource_seeds)
        math(EXPR _beam_resource_count "${_beam_resource_count} + 1")
        file(SIZE "${_beam_seed}" _beam_seed_size)
        get_filename_component(_beam_module "${_beam_seed}" NAME_WE)
        string(LENGTH "${_beam_module}" _beam_name_length)
        string(APPEND _beam_resource_code "${_beam_resource_count} ERLANG_CODE \"ebin/${_beam_module}.beam\"\n")
        string(APPEND _beam_resource_dict "    ${_beam_seed_size}L, ${_beam_resource_count}, ${_beam_name_length},\"${_beam_module}\",\n")
    endforeach()
    file(WRITE "${CMAKE_BINARY_DIR}/sdk/beams.rc"
        "/* Canonical execution images for the installed BEAM SDK. */\n${_beam_resource_code}#include <beam.rc>\n\n0 ERLANG_DICT\nBEGIN\n    ${_beam_resource_count},\n${_beam_resource_dict}END\n")
    install(FILES "${CMAKE_BINARY_DIR}/sdk/beams.rc"
        "${CMAKE_SOURCE_DIR}/erts/emulator/sys/win32/beam.rc"
        DESTINATION "${BEAM_SDK_PRELOAD_RESOURCE_DIR}")
    install(FILES ${_beam_resource_seeds} DESTINATION "${BEAM_SDK_PRELOAD_RESOURCE_DIR}/ebin")
endif()
# Installed metadata is data rather than four conflicting target exports. The
# same package selector can consume one installed configuration or an SDK bundle.
set(_beam_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/BEAM")
configure_package_config_file("${CMAKE_SOURCE_DIR}/cmake/BEAMConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/BEAMConfig.cmake" INSTALL_DESTINATION "${_beam_cmake_dir}")
write_basic_package_version_file("${CMAKE_BINARY_DIR}/BEAMConfigVersion.cmake"
    VERSION "${BEAM_SDK_VERSION}" COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_BINARY_DIR}/BEAMConfig.cmake"
    "${CMAKE_BINARY_DIR}/BEAMConfigVersion.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/BEAMInstall.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/AssembleSDK.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/SDKManifest.cmake" DESTINATION "${_beam_cmake_dir}")

set(_beam_toolchain_identity "BEAM-SDK-toolchain-v1")
foreach(_beam_key CMAKE_C_COMPILER_ID CMAKE_C_COMPILER_VERSION CMAKE_C_COMPILER_TARGET
        CMAKE_CXX_COMPILER_ID CMAKE_CXX_COMPILER_VERSION CMAKE_CXX_COMPILER_TARGET
        MSVC_VERSION CMAKE_SIZEOF_VOID_P CMAKE_C_STANDARD CMAKE_CXX_STANDARD)
    string(APPEND _beam_toolchain_identity "\n${_beam_key}=${${_beam_key}}")
endforeach()
foreach(_beam_tool CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
    file(SHA256 "${${_beam_tool}}" _beam_hash)
    string(APPEND _beam_toolchain_identity "\n${_beam_tool}:${_beam_hash}")
endforeach()
if(EXISTS "${CMAKE_OSX_SYSROOT}/SDKSettings.json")
    file(SHA256 "${CMAKE_OSX_SYSROOT}/SDKSettings.json" _beam_hash)
    string(APPEND _beam_toolchain_identity "\nsysroot-settings:${_beam_hash}")
endif()
string(SHA256 BEAM_SDK_TOOLCHAIN_ID "${_beam_toolchain_identity}")

set(BEAM_SDK_MANIFEST "{}")
string(JSON BEAM_SDK_MANIFEST SET "${BEAM_SDK_MANIFEST}" schema 1)
foreach(_beam_field version erts_version upstream_commit source_id system_name architecture
        host_triple deployment_target dynamic_trace toolchain_id)
    string(TOUPPER "${_beam_field}" _beam_upper)
    beam_sdk_json_quote(_beam_value "${BEAM_SDK_${_beam_upper}}")
    string(JSON BEAM_SDK_MANIFEST SET "${BEAM_SDK_MANIFEST}" ${_beam_field} "${_beam_value}")
endforeach()
set(_beam_runtime_family "${BEAM_SDK_MSVC_RUNTIME_LIBRARY}")
string(REPLACE "Debug" "" _beam_runtime_family "${_beam_runtime_family}")
beam_sdk_json_quote(_beam_value "${_beam_runtime_family}")
string(JSON BEAM_SDK_MANIFEST SET "${BEAM_SDK_MANIFEST}" msvc_runtime_family "${_beam_value}")
foreach(_beam_pair "include_dir|${CMAKE_INSTALL_INCLUDEDIR}"
        "native_include_dir|${BEAM_SDK_NATIVE_INCLUDE_DIR}"
        "preload_ebin_dir|${BEAM_SDK_PRELOAD_EBIN_DIR}"
        "preload_source_dir|${BEAM_SDK_PRELOAD_SOURCE_DIR}"
        "preload_resource_dir|${BEAM_SDK_PRELOAD_RESOURCE_DIR}"
        "package_dir|${_beam_cmake_dir}")
    string(REPLACE "|" ";" _beam_pair "${_beam_pair}")
    list(GET _beam_pair 0 _beam_key)
    list(GET _beam_pair 1 _beam_value)
    if(IS_ABSOLUTE "${_beam_value}" OR "${_beam_value}" MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "SDK metadata requires relative install directories: ${_beam_value}")
    endif()
    beam_sdk_json_quote(_beam_quoted "${_beam_value}")
    string(JSON BEAM_SDK_MANIFEST SET "${BEAM_SDK_MANIFEST}" ${_beam_key} "${_beam_quoted}")
endforeach()
set(_beam_package_files "")
foreach(_beam_file BEAMConfig.cmake BEAMConfigVersion.cmake BEAMInstall.cmake AssembleSDK.cmake SDKManifest.cmake)
    list(APPEND _beam_package_files "${_beam_cmake_dir}/${_beam_file}")
endforeach()
beam_sdk_json_array(_beam_array ${_beam_package_files})
string(JSON BEAM_SDK_MANIFEST SET "${BEAM_SDK_MANIFEST}" package_files "${_beam_array}")
set(_beam_common_files "${CMAKE_INSTALL_INCLUDEDIR}/beam/beam.h")
foreach(_beam_header IN LISTS _beam_sdk_native_headers)
    get_filename_component(_beam_name "${_beam_header}" NAME)
    list(APPEND _beam_common_files "${BEAM_SDK_NATIVE_INCLUDE_DIR}/${_beam_name}")
endforeach()
foreach(_beam_seed IN LISTS _beam_inspection_seeds)
    get_filename_component(_beam_name "${_beam_seed}" NAME)
    list(APPEND _beam_common_files "${BEAM_SDK_PRELOAD_EBIN_DIR}/${_beam_name}")
endforeach()
list(APPEND _beam_common_files "${BEAM_SDK_PRELOAD_EBIN_DIR}/erts.app")
file(GLOB_RECURSE _beam_preload_sources CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    RELATIVE "${CMAKE_SOURCE_DIR}/erts/preloaded/src" "${CMAKE_SOURCE_DIR}/erts/preloaded/src/*")
foreach(_beam_source IN LISTS _beam_preload_sources)
    list(APPEND _beam_common_files "${BEAM_SDK_PRELOAD_SOURCE_DIR}/${_beam_source}")
endforeach()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND _beam_common_files "${BEAM_SDK_PRELOAD_RESOURCE_DIR}/beams.rc"
        "${BEAM_SDK_PRELOAD_RESOURCE_DIR}/beam.rc")
    foreach(_beam_seed IN LISTS _beam_resource_seeds)
        get_filename_component(_beam_name "${_beam_seed}" NAME)
        list(APPEND _beam_common_files "${BEAM_SDK_PRELOAD_RESOURCE_DIR}/ebin/${_beam_name}")
    endforeach()
endif()
list(SORT _beam_common_files)
beam_sdk_json_array(_beam_array ${_beam_common_files})
string(JSON BEAM_SDK_MANIFEST SET "${BEAM_SDK_MANIFEST}" common_files "${_beam_array}")

get_target_property(_beam_archive_name beam_runtime_archive OUTPUT_NAME)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    get_target_property(_beam_postfix beam_runtime_archive DEBUG_POSTFIX)
    string(APPEND _beam_archive_name "${_beam_postfix}")
endif()
set(_beam_archive "${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${_beam_archive_name}${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_beam_executable "${CMAKE_INSTALL_BINDIR}/beam${CMAKE_EXECUTABLE_SUFFIX}")
set(_beam_helper "")
set(_beam_pdb "")
set(_beam_variant_files "${_beam_archive}" "${_beam_executable}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_beam_pdb "${CMAKE_INSTALL_BINDIR}/beam.pdb")
        list(APPEND _beam_variant_files "${_beam_pdb}")
    endif()
else()
    set(_beam_helper "${CMAKE_INSTALL_BINDIR}/erl_child_setup")
    list(APPEND _beam_variant_files "${_beam_helper}")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _beam_variant_files "${CMAKE_INSTALL_BINDIR}/erl_child_setup.debug")
    endif()
endif()
set(_beam_variant "{}")
foreach(_beam_pair "flavor|${BEAM_SDK_FLAVOR}" "build_type|${BEAM_SDK_BUILD_TYPE}"
        "build_id|${BEAM_SDK_BUILD_ID}" "msvc_runtime_library|${BEAM_SDK_MSVC_RUNTIME_LIBRARY}"
        "archive|${_beam_archive}" "executable|${_beam_executable}"
        "child_setup|${_beam_helper}" "pdb|${_beam_pdb}")
    string(FIND "${_beam_pair}" "|" _beam_separator)
    string(SUBSTRING "${_beam_pair}" 0 ${_beam_separator} _beam_key)
    math(EXPR _beam_separator "${_beam_separator} + 1")
    string(SUBSTRING "${_beam_pair}" ${_beam_separator} -1 _beam_value)
    beam_sdk_json_quote(_beam_quoted "${_beam_value}")
    string(JSON _beam_variant SET "${_beam_variant}" ${_beam_key} "${_beam_quoted}")
endforeach()
get_target_property(_beam_language beam_runtime_archive LINKER_LANGUAGE)
beam_sdk_json_quote(_beam_quoted "${_beam_language}")
string(JSON _beam_variant SET "${_beam_variant}" link_language "${_beam_quoted}")
get_target_property(_beam_links beam_runtime_static INTERFACE_LINK_LIBRARIES)
list(FILTER _beam_links EXCLUDE REGEX "LINK_LIBRARY:WHOLE_ARCHIVE")
beam_sdk_json_array(_beam_array ${_beam_links})
string(JSON _beam_variant SET "${_beam_variant}" link_libraries "${_beam_array}")
get_target_property(_beam_options beam_runtime_static INTERFACE_LINK_OPTIONS)
if(_beam_options STREQUAL "_beam_options-NOTFOUND")
    set(_beam_options "")
endif()
beam_sdk_json_array(_beam_array ${_beam_options})
string(JSON _beam_variant SET "${_beam_variant}" link_options "${_beam_array}")
beam_sdk_json_array(_beam_array ${_beam_variant_files})
string(JSON _beam_variant SET "${_beam_variant}" files "${_beam_array}")
string(JSON BEAM_SDK_MANIFEST SET "${BEAM_SDK_MANIFEST}" variants "[${_beam_variant}]")
configure_file("${CMAKE_SOURCE_DIR}/cmake/InstallSDKManifest.cmake.in"
    "${CMAKE_BINARY_DIR}/sdk/InstallSDKManifest.cmake" @ONLY)
install(SCRIPT "${CMAKE_BINARY_DIR}/sdk/InstallSDKManifest.cmake")
message(STATUS "BEAM SDK ${BEAM_SDK_VERSION} ${BEAM_SDK_BUILD_TYPE}: ${BEAM_SDK_BUILD_ID}")
