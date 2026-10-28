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
            message(FATAL_ERROR "Static NIFs require MODULE with their Erlang module name")
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
