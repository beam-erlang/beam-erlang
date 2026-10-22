# SPDX-License-Identifier: Apache-2.0
# Matching-build stamp for explicitly selected native modules. Not a stable ABI.
function(beam_target_nif target)
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
" @ONLY)
    target_sources("${target}" PRIVATE "${_stamp}")
    target_link_libraries("${target}" PRIVATE BEAM::NIF)
    if(WIN32)
        target_link_libraries("${target}" PRIVATE BEAM::Runtime)
    endif()
endfunction()
