# SPDX-License-Identifier: Apache-2.0
# Producer-owned target facts. Source-attested compiler identity is independent
# of runtime flavor, native compiler, build host and CI route names.
set(_compiler_manifest "${CMAKE_SOURCE_DIR}/src/beam/compiler/seeds/manifest.json")
file(SHA256 "${_compiler_manifest}" BEAM_COMPILER_GENERATION)
set(_native_kinds "\"shared\"")
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_native_kinds "\"static\"")
endif()
set(_compiler_environment "${CMAKE_C_COMPILER_ID}")
if(CMAKE_C_SIMULATE_ID)
    string(APPEND _compiler_environment "-${CMAKE_C_SIMULATE_ID}")
elseif(MINGW)
    string(APPEND _compiler_environment "-MinGW")
endif()
set(_baseline "${BEAM_TARGET_MINIMUM}")
if(NOT _baseline)
    set(_baseline unspecified)
endif()
set(_apple_variant "${BEAM_APPLE_VARIANT}")
if(NOT _apple_variant)
    set(_apple_variant none)
endif()
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/target.toml" CONTENT
"format = 1
sdk-build-id = \"${BEAM_COMPONENT_BUILD_ID}\"
system = \"${CMAKE_SYSTEM_NAME}\"
architecture = \"${BEAM_TARGET_ARCH}\"
processor = \"${CMAKE_SYSTEM_PROCESSOR}\"
abi = \"${ERTS_HOST_TRIPLE}\"
libc = \"${BEAM_TARGET_LIBC}\"
compiler-environment = \"${_compiler_environment}\"
native-compiler-version = \"${CMAKE_C_COMPILER_VERSION}\"
flavor = \"${ERTS_BUILD_FLAVOR}\"
baseline = \"${_baseline}\"
apple-variant = \"${_apple_variant}\"
native-kinds = [${_native_kinds}]
compiler-generation = \"${BEAM_COMPILER_GENERATION}\"
" @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/target.toml" DESTINATION lib/beam)
install(FILES "${_compiler_manifest}" DESTINATION lib/beam RENAME compiler-generation.json)
install(FILES "${CMAKE_SOURCE_DIR}/cmake/BEAMVerifySDK.cmake" DESTINATION lib/cmake/BEAM)
