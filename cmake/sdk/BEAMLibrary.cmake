# SPDX-License-Identifier: Apache-2.0
include_guard(GLOBAL)

# An author supplies compiled modules and typed, statically compiled NIF targets.
# No runtime selection, acquisition or startup is implied by this declaration.
function(beam_target_library target)
    cmake_parse_arguments(PARSE_ARGV 1 LIB "" "NAME" "MODULES;BEAM_FILES;NIF_TARGETS")
    if(NOT TARGET "${target}" OR LIB_UNPARSED_ARGUMENTS OR LIB_KEYWORDS_MISSING_VALUES
       OR NOT LIB_NAME MATCHES "^[a-zA-Z][a-zA-Z0-9_.-]*$")
        message(FATAL_ERROR "beam_target_library requires a target, NAME, MODULES and BEAM_FILES")
    endif()
    list(LENGTH LIB_MODULES _count)
    list(LENGTH LIB_BEAM_FILES _files)
    if(NOT _count EQUAL _files)
        message(FATAL_ERROR "Library MODULES and BEAM_FILES must correspond one for one")
    endif()
    get_target_property(_kind "${target}" TYPE)
    if(NOT _kind MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY)$")
        message(FATAL_ERROR "A BEAM library target must be static or shared")
    endif()
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    string(SHA256 _unique "${BEAM_SDK_BUILD_ID}:${LIB_NAME}:${target}")
    set(_accessor "beam_library_${_unique}")
    set(_header "${CMAKE_CURRENT_BINARY_DIR}/${target}-beam-library.h")
    set(_source "${CMAKE_CURRENT_BINARY_DIR}/${target}-beam-library.c")
    set(_args --name "${LIB_NAME}" --build-id "${BEAM_SDK_BUILD_ID}" --accessor "${_accessor}" --output "${_source}")
    if(NOT _kind STREQUAL "STATIC_LIBRARY")
        list(APPEND _args --shared)
    endif()
    set(_seen)
    if(_count)
        math(EXPR _last "${_count} - 1")
        foreach(_i RANGE ${_last})
            list(GET LIB_MODULES ${_i} _module)
            list(GET LIB_BEAM_FILES ${_i} _file)
            if(NOT _module MATCHES "^[a-z][a-zA-Z0-9_@]*$" OR _module IN_LIST _seen)
                message(FATAL_ERROR "Invalid or duplicate library module: ${_module}")
            endif()
            list(APPEND _seen "${_module}")
            list(APPEND _args --module "${_module}" "${_file}")
        endforeach()
    endif()
    set(_seen)
    foreach(_nif IN LISTS LIB_NIF_TARGETS)
        get_target_property(_type "${_nif}" BEAM_NIF_KIND)
        get_target_property(_identity "${_nif}" BEAM_NIF_SDK_BUILD_ID)
        get_target_property(_module "${_nif}" BEAM_NIF_MODULE)
        get_target_property(_initialize "${_nif}" BEAM_NIF_RESIDENT_INITIALIZER)
        if(NOT _type STREQUAL "static" OR NOT _identity STREQUAL BEAM_SDK_BUILD_ID
           OR NOT _initialize OR _module IN_LIST _seen)
            message(FATAL_ERROR "Library native targets require unique matching-SDK static NIF registrations: ${_nif}")
        endif()
        list(APPEND _seen "${_module}")
        list(APPEND _args --nif "${_module}" "${_initialize}")
    endforeach()
    set(_tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../../share/beam/tools/beam-library.py")
    add_custom_command(OUTPUT "${_source}"
        COMMAND "${Python3_EXECUTABLE}" "${_tool}" ${_args}
        DEPENDS "${_tool}" ${LIB_BEAM_FILES} VERBATIM)
    file(CONFIGURE OUTPUT "${_header}" CONTENT "/* Generated matching-SDK passive library accessor. */
#include <beam/library.h>
#ifdef __cplusplus
extern \"C\" {
#endif
const beam_library_descriptor *${_accessor}(void);
#ifdef __cplusplus
}
#endif
" @ONLY)
    target_sources("${target}" PRIVATE "${_source}")
    target_link_libraries("${target}" PRIVATE ${LIB_NIF_TARGETS})
    target_include_directories("${target}" PRIVATE "${_BEAM_ARTIFACT_PREFIX}/include")
    set_target_properties("${target}" PROPERTIES POSITION_INDEPENDENT_CODE ON
        BEAM_LIBRARY_ACCESSOR "${_accessor}" BEAM_LIBRARY_HEADER "${_header}"
        BEAM_LIBRARY_SDK_BUILD_ID "${BEAM_SDK_BUILD_ID}")
endfunction()
