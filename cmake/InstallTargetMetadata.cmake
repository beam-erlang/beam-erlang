# SPDX-License-Identifier: Apache-2.0
# Producer-owned target facts. Source-attested compiler identity is independent
# of runtime flavor, native compiler, build host and CI route names.
set(_compiler_manifest "${CMAKE_SOURCE_DIR}/src/beam/compiler/seeds/manifest.json")
file(SHA256 "${_compiler_manifest}" BEAM_COMPILER_GENERATION)
set(_native_kinds "\"shared\"")
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_native_kinds "\"static\"")
elseif(ANDROID)
    set(_native_kinds "\"static\", \"shared\"")
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
set(_android_facts "")
if(ANDROID)
    file(STRINGS "${CMAKE_ANDROID_NDK}/source.properties" _ndk_revision REGEX "^Pkg.Revision")
    string(REGEX REPLACE "^Pkg.Revision[ =]+" "" BEAM_ANDROID_NDK_VERSION "${_ndk_revision}")
    string(STRIP "${BEAM_ANDROID_NDK_VERSION}" BEAM_ANDROID_NDK_VERSION)
    set(_android_facts "android-abi = \"${ANDROID_ABI}\"\nndk-version = \"${BEAM_ANDROID_NDK_VERSION}\"\ncxx-runtime = \"c++_shared\"\n")
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
${_android_facts}" NEWLINE_STYLE UNIX @ONLY)
_beam_install_sdk_file(sdk.target target-metadata "${CMAKE_BINARY_DIR}/target.toml"
    lib/beam/target.toml REQUIRES compiler.generation)
_beam_install_sdk_file(compiler.generation compiler-provenance "${_compiler_manifest}"
    lib/beam/compiler-generation.json)
install(FILES "${CMAKE_SOURCE_DIR}/cmake/sdk/BEAMVerifySDK.cmake" DESTINATION lib/cmake/BEAM/support)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(BEAM_CONSUMER_SYSROOT iphoneos)
    if(BEAM_APPLE_VARIANT STREQUAL "simulator")
        set(BEAM_CONSUMER_SYSROOT iphonesimulator)
    endif()
    configure_file("${CMAKE_SOURCE_DIR}/cmake/sdk/BEAMiOSToolchain.cmake.in"
        "${CMAKE_BINARY_DIR}/BEAMToolchain.cmake" @ONLY)
    _beam_install_sdk_file(sdk.toolchain consumer-toolchain "${CMAKE_BINARY_DIR}/BEAMToolchain.cmake"
        lib/cmake/BEAM/BEAMToolchain.cmake REQUIRES sdk.target)
endif()

if(ANDROID)
    configure_file("${CMAKE_SOURCE_DIR}/cmake/sdk/BEAMAndroidToolchain.cmake.in"
        "${CMAKE_BINARY_DIR}/BEAMToolchain.cmake" @ONLY)
    _beam_install_sdk_file(sdk.toolchain consumer-toolchain "${CMAKE_BINARY_DIR}/BEAMToolchain.cmake"
        lib/cmake/BEAM/BEAMToolchain.cmake REQUIRES sdk.target)
endif()
