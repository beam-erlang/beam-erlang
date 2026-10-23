# A component must match the runtime's native sources, bytecode and build configuration.
# Hash paths relative to the source tree so relocation does not change the identity.
file(GLOB_RECURSE _component_identity_sources CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/src/*" "${CMAKE_SOURCE_DIR}/cmake/*")
list(FILTER _component_identity_sources EXCLUDE REGEX "(__pycache__|[.]pyc$)")
list(APPEND _component_identity_sources
    "${CMAKE_SOURCE_DIR}/CMakeLists.txt"
    "${CMAKE_SOURCE_DIR}/BEAM_VERSION"
    ${BEAM_CORE_BEAM_FILES})
list(REMOVE_DUPLICATES _component_identity_sources)
list(SORT _component_identity_sources)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_component_identity_sources})
# Replace the longer root first: source/build may be nested or share a prefix.
string(LENGTH "${CMAKE_SOURCE_DIR}" _source_root_length)
string(LENGTH "${CMAKE_BINARY_DIR}" _binary_root_length)
if(_source_root_length GREATER _binary_root_length)
    set(_identity_roots "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set(_identity_root_labels "<source>" "<build>")
else()
    set(_identity_roots "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}")
    set(_identity_root_labels "<build>" "<source>")
endif()
function(_beam_normalize_identity_paths INPUT OUTPUT)
    set(_normalized "${INPUT}")
    foreach(_index RANGE 0 1)
        list(GET _identity_roots ${_index} _root)
        list(GET _identity_root_labels ${_index} _label)
        string(REPLACE "${_root}" "${_label}" _normalized "${_normalized}")
    endforeach()
    set(${OUTPUT} "${_normalized}" PARENT_SCOPE)
endfunction()
set(_identity "")
foreach(_setting IN ITEMS PROJECT_VERSION BEAM_RELEASE_VERSION CMAKE_BUILD_TYPE
        ERTS_BUILD_FLAVOR ERTS_TARGET_ARCH ERTS_HOST_TRIPLE
        CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR BEAM_TARGET_LIBC BEAM_TARGET_MINIMUM
        BEAM_APPLE_VARIANT CMAKE_OSX_DEPLOYMENT_TARGET ANDROID_ABI ANDROID_STL BEAM_SANITIZERS
        CMAKE_C_COMPILER_ID CMAKE_C_COMPILER_VERSION CMAKE_C_COMPILER_TARGET
        CMAKE_CXX_COMPILER_ID CMAKE_CXX_COMPILER_VERSION CMAKE_CXX_COMPILER_TARGET
        CMAKE_ASM_COMPILER_ID CMAKE_ASM_COMPILER_VERSION CMAKE_ASM_COMPILER_TARGET
        CMAKE_C_STANDARD CMAKE_C_STANDARD_REQUIRED CMAKE_C_EXTENSIONS
        CMAKE_CXX_STANDARD CMAKE_CXX_STANDARD_REQUIRED CMAKE_CXX_EXTENSIONS
        CMAKE_POSITION_INDEPENDENT_CODE CMAKE_INTERPROCEDURAL_OPTIMIZATION
        CMAKE_C_VISIBILITY_PRESET CMAKE_CXX_VISIBILITY_PRESET
        CMAKE_VISIBILITY_INLINES_HIDDEN
        CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_ASM_FLAGS
        CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
    string(APPEND _identity "${_setting}=${${_setting}}\n")
endforeach()
string(TOUPPER "${CMAKE_BUILD_TYPE}" _identity_build_type)
foreach(_prefix IN ITEMS CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_ASM_FLAGS
        CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS
        CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set(_setting "${_prefix}_${_identity_build_type}")
    string(APPEND _identity "${_setting}=${${_setting}}\n")
endforeach()
foreach(_source IN LISTS _component_identity_sources)
    file(RELATIVE_PATH _relative "${CMAKE_SOURCE_DIR}" "${_source}")
    file(SHA256 "${_source}" _digest)
    string(APPEND _identity "${_relative}:${_digest}\n")
endforeach()
# Only these configure-generated headers are part of the identity. Build-generated
# YCF and emulator headers must not make a clean build change identity later.
set(_component_identity_config
    "${CMAKE_BINARY_DIR}/erts/config.h"
    "${CMAKE_BINARY_DIR}/erts/erl_int_sizes_config.h"
    "${CMAKE_BINARY_DIR}/erts/include/erl_int_sizes_config.h"
    "${CMAKE_BINARY_DIR}/erts/include/internal/ethread_header_config.h"
    "${CMAKE_BINARY_DIR}/erts/beam_core_modules.h")
foreach(_config IN LISTS _component_identity_config)
    file(RELATIVE_PATH _relative "${CMAKE_BINARY_DIR}" "${_config}")
    file(READ "${_config}" _content)
    _beam_normalize_identity_paths("${_content}" _content)
    string(SHA256 _digest "${_content}")
    string(APPEND _identity "${_relative}:${_digest}\n")
endforeach()
_beam_normalize_identity_paths("${_identity}" _identity)
string(SHA256 BEAM_COMPONENT_BUILD_ID "${_identity}")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/components/include")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/components/include/beam_component_build.h"
    CONTENT "/* Generated matching-build component identity. */\n#ifndef BEAM_COMPONENT_BUILD_H\n#define BEAM_COMPONENT_BUILD_H\n#define BEAM_COMPONENT_BUILD_ID \"${BEAM_COMPONENT_BUILD_ID}\"\n#endif\n" @ONLY)
unset(_identity)
