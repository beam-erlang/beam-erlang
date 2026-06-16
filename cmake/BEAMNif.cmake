# SPDX-License-Identifier: Apache-2.0
# Matching-build stamp for explicitly selected native modules. Not a stable ABI.
function(beam_target_nif target)
    beam_check_native_target()
    cmake_parse_arguments(NIF "" "MODULE" "" ${ARGN})
    if(NIF_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "beam_target_nif accepts MODULE for an explicit Erlang module")
    endif()
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Unknown NIF target: ${target}")
    endif()
    file(STRINGS "${BEAM_SDK_METADATA}" _identity REGEX "^build-id = ")
    string(REGEX MATCH "[a-f0-9]+\"$" _identity "${_identity}")
    string(REPLACE "\"" "" _identity "${_identity}")
    string(LENGTH "${_identity}" _length)
    if(NOT _length EQUAL 64)
        message(FATAL_ERROR "Invalid BEAM SDK provenance")
    endif()
    get_property(_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    if("C" IN_LIST _languages)
        set(_extension c)
    else()
        set(_extension cpp)
    endif()
    get_target_property(_kind "${target}" TYPE)
    if(_kind STREQUAL "STATIC_LIBRARY")
        set_target_properties("${target}" PROPERTIES POSITION_INDEPENDENT_CODE ON)
        if(NOT NIF_MODULE MATCHES "^[a-z][a-zA-Z0-9_@]*$")
            message(FATAL_ERROR "Static package NIFs require MODULE with their Erlang module name")
        endif()
        string(SHA256 _unique "${_identity}:${NIF_MODULE}:${target}")
        set(_symbol "beam_package_${_unique}")
        # CMake target names are globally unique; keep generated filenames short
        # enough for native Windows object paths while retaining the full symbol.
        set(_filename "${target}-beam-registration")
        set(_header "${CMAKE_CURRENT_BINARY_DIR}/${_filename}.h")
        set(_source "${CMAKE_CURRENT_BINARY_DIR}/${_filename}.${_extension}")
        file(CONFIGURE OUTPUT "${_header}" CONTENT "/* Generated matching-SDK built-in NIF registration. */
#include <beam/beam.h>
#ifdef __cplusplus
extern \"C\" {
#endif
const beam_builtin_nif *${_symbol}_registration(void);
const beam_linked_nif_spec *${_symbol}_binding(void);
#ifdef __cplusplus
}
#endif
" NEWLINE_STYLE UNIX @ONLY)
        file(CONFIGURE OUTPUT "${_source}" CONTENT "/* Generated matching-SDK built-in NIF registration. */
#include \"${_filename}.h\"
#include <erl_nif.h>
#ifdef __cplusplus
extern \"C\" {
#endif
extern ErlNifEntry *${_symbol}_nif_init(ERL_NIF_INIT_ARGS);
#ifdef __cplusplus
}
#endif
static ErlNifEntry *${_symbol}_initialize(void) {
    return ${_symbol}_nif_init(
#ifdef _WIN32
        NULL
#endif
    );
}
static const beam_builtin_nif registration = {
    sizeof(beam_builtin_nif), \"${NIF_MODULE}\", \"${_identity}\", ${_symbol}_initialize, 1
};
const beam_builtin_nif *${_symbol}_registration(void) { return &registration; }
static const beam_linked_nif_spec binding = {
    sizeof(beam_linked_nif_spec), \"${NIF_MODULE}\", \"${_identity}\", ${_symbol}_initialize
};
const beam_linked_nif_spec *${_symbol}_binding(void) { return &binding; }
" NEWLINE_STYLE UNIX @ONLY)
        target_sources("${target}" PRIVATE "${_source}")
        target_compile_definitions("${target}" PRIVATE "STATIC_ERLANG_NIF_LIBNAME=${_symbol}")
        target_include_directories("${target}" PRIVATE "${_BEAM_ARTIFACT_PREFIX}/include")
        target_link_libraries("${target}" PRIVATE BEAM::NIF)
        set_target_properties("${target}" PROPERTIES
            BEAM_NIF_KIND static BEAM_NIF_MODULE "${NIF_MODULE}"
            BEAM_NIF_INITIALIZER "${_symbol}_nif_init"
            BEAM_NIF_REGISTRATION "${_symbol}_registration"
            BEAM_NIF_BINDING "${_symbol}_binding"
            BEAM_NIF_HEADER "${_header}" BEAM_NIF_SDK_BUILD_ID "${_identity}")
        return()
    endif()
    if(NOT _kind MATCHES "^(MODULE_LIBRARY|SHARED_LIBRARY)$")
        message(FATAL_ERROR "NIF targets must be module, shared or static libraries")
    endif()
    set_target_properties("${target}" PROPERTIES BEAM_NIF_KIND shared
        BEAM_NIF_MODULE "${NIF_MODULE}" BEAM_NIF_SDK_BUILD_ID "${_identity}")
    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/${target}-beam-sdk.${_extension}")
    file(CONFIGURE OUTPUT "${_stamp}" CONTENT "/* Generated matching-build NIF provenance. */
#ifdef __cplusplus
extern \"C\" {
#endif
#ifdef _WIN32
__declspec(dllexport) extern const char beam_nif_sdk_v1[];
#else
__attribute__((visibility(\"default\"))) extern const char beam_nif_sdk_v1[];
#endif
const char beam_nif_sdk_v1[] = \"${_identity}\";
#ifdef __cplusplus
}
#endif
" NEWLINE_STYLE UNIX @ONLY)
    target_sources("${target}" PRIVATE "${_stamp}")
    target_link_libraries("${target}" PRIVATE BEAM::NIF)
    # Windows NIFs bind through their native callback table. Linking a runtime
    # DLL here would introduce a second VM into a static embedding host.
    if(ANDROID)
        target_link_libraries("${target}" PRIVATE BEAM::Runtime)
    endif()
endfunction()

# A logical package output describes its complete installed link closure.
function(_beam_package_native_output_finalize target)
    cmake_parse_arguments(OUTPUT "" "NAME" "" ${ARGN})
    if(OUTPUT_UNPARSED_ARGUMENTS OR NOT OUTPUT_NAME MATCHES "^[A-Za-z0-9_-]+$" OR NOT TARGET "${target}")
        message(FATAL_ERROR "beam_package_native_output requires a target and NAME")
    endif()
    get_target_property(_kind "${target}" TYPE)
    if(_kind STREQUAL "STATIC_LIBRARY")
        set(_artifact_kind static)
    elseif(_kind MATCHES "^(MODULE_LIBRARY|SHARED_LIBRARY)$")
        set(_artifact_kind shared)
    else()
        message(FATAL_ERROR "Package native outputs must be libraries")
    endif()
    set(_files "\"priv/$<TARGET_FILE_NAME:${target}>\"")
    set(_extra "")
    get_target_property(_binding "${target}" BEAM_NIF_BINDING)
    if(_binding)
        get_target_property(_header "${target}" BEAM_NIF_HEADER)
        get_target_property(_module "${target}" BEAM_NIF_MODULE)
        get_filename_component(_header_name "${_header}" NAME)
        string(APPEND _files ", \"priv/${_header_name}\"")
        string(APPEND _extra "module = \"${_module}\"\nbinding = \"${_binding}\"\nheader = \"priv/${_header_name}\"\n")
        install(FILES "${_header}" DESTINATION priv)
    endif()
    set(_dependencies "")
    foreach(_property LINK_OPTIONS INTERFACE_LINK_OPTIONS LINK_DIRECTORIES INTERFACE_LINK_DIRECTORIES)
        get_target_property(_unsupported "${target}" "${_property}")
        if(_unsupported)
            message(FATAL_ERROR "Unsupported native dependency closure: ${_property}=${_unsupported}")
        endif()
    endforeach()
    get_target_property(_links "${target}" LINK_LIBRARIES)
    get_target_property(_interface "${target}" INTERFACE_LINK_LIBRARIES)
    if(NOT _links)
        set(_links "")
    endif()
    if(_interface)
        list(APPEND _links ${_interface})
    endif()
    list(REMOVE_DUPLICATES _links)
    if(_links)
        foreach(_link IN LISTS _links)
            if(_link MATCHES "^\\$<LINK_ONLY:([^<>]+)>$")
                set(_link "${CMAKE_MATCH_1}")
            endif()
            if(_link STREQUAL "BEAM::NIF")
                continue()
            elseif(_link STREQUAL "BEAM::Runtime")
                set(_dependency sdk:runtime)
            elseif(_link STREQUAL "m" OR _link STREQUAL "c++")
                set(_dependency "system:${_link}")
            elseif(ANDROID AND _link MATCHES "^(android|log|dl|c[+][+]_shared)$")
                set(_dependency "system:${_link}")
            elseif(TARGET "${_link}")
                get_target_property(_logical "${_link}" BEAM_PACKAGE_OUTPUT_NAME)
                if(NOT _logical)
                    message(FATAL_ERROR "Native dependency ${_link} must be registered as a package output")
                endif()
                set(_dependency "output:${_logical}")
            else()
                message(FATAL_ERROR "Unsupported native dependency closure: ${_link}")
            endif()
            list(APPEND _dependencies "\"${_dependency}\"")
        endforeach()
    endif()
    list(REMOVE_DUPLICATES _dependencies)
    string(JOIN ", " _dependencies ${_dependencies})
    file(STRINGS "${BEAM_SDK_METADATA}" _identity REGEX "^build-id = ")
    string(REPLACE "build-id = " "" _identity "${_identity}")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/beam-native-outputs/${OUTPUT_NAME}.toml"
        CONTENT "name = \"${OUTPUT_NAME}\"\npath = \"priv/$<TARGET_FILE_NAME:${target}>\"\nkind = \"${_artifact_kind}\"\nfiles = [${_files}]\ndependencies = [${_dependencies}]\nlanguage = \"$<TARGET_PROPERTY:${target},LINKER_LANGUAGE>\"\nsdk-build-id = ${_identity}\n${_extra}")
    install(TARGETS "${target}" ARCHIVE DESTINATION priv LIBRARY DESTINATION priv RUNTIME DESTINATION priv)
endfunction()

# Inspect the final target graph, after recipes have completed target mutation.
function(_beam_package_native_outputs_finalize)
    get_property(_targets GLOBAL PROPERTY BEAM_PACKAGE_NATIVE_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_name "${_target}" BEAM_PACKAGE_OUTPUT_NAME)
        _beam_package_native_output_finalize("${_target}" NAME "${_name}")
    endforeach()
endfunction()

function(beam_package_native_output target)
    cmake_parse_arguments(OUTPUT "" "NAME" "" ${ARGN})
    if(OUTPUT_UNPARSED_ARGUMENTS OR NOT OUTPUT_NAME MATCHES "^[A-Za-z0-9_-]+$" OR NOT TARGET "${target}")
        message(FATAL_ERROR "beam_package_native_output requires a target and NAME")
    endif()
    get_property(_names GLOBAL PROPERTY BEAM_PACKAGE_NATIVE_NAMES)
    get_target_property(_registered "${target}" BEAM_PACKAGE_OUTPUT_NAME)
    if(OUTPUT_NAME IN_LIST _names OR _registered)
        message(FATAL_ERROR "Duplicate native output name or target: ${OUTPUT_NAME}")
    endif()
    set_property(GLOBAL APPEND PROPERTY BEAM_PACKAGE_NATIVE_NAMES "${OUTPUT_NAME}")
    set_property(GLOBAL APPEND PROPERTY BEAM_PACKAGE_NATIVE_TARGETS "${target}")
    set_property(TARGET "${target}" PROPERTY BEAM_PACKAGE_OUTPUT_NAME "${OUTPUT_NAME}")
    get_property(_scheduled GLOBAL PROPERTY BEAM_PACKAGE_NATIVE_FINALIZE_SCHEDULED)
    if(NOT _scheduled)
        set_property(GLOBAL PROPERTY BEAM_PACKAGE_NATIVE_FINALIZE_SCHEDULED TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _beam_package_native_outputs_finalize)
    endif()
endfunction()
